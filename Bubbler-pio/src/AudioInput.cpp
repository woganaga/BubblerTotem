#include "AudioInput.h"
#include "ESP_I2S.h"
#include "LittleFS.h"
#include <arduinoFFT.h>

static const char* SETTINGS_FILE = "/mic_settings.bin";

#define MIC_SCK_PIN  44
#define MIC_WS_PIN   7
#define MIC_SD_PIN   8

#define STATUS_LED_PIN 21 // onboard status LED, active low (pull low to light)

#define SAMPLE_RATE   16000
#define FFT_SAMPLES   512                              // ~32 FFT frames/sec at 16kHz
#define BIN_HZ        ((float)SAMPLE_RATE / FFT_SAMPLES) // 31.25 Hz per bin
#define FPS           ((float)SAMPLE_RATE / FFT_SAMPLES) // frame (onset) rate

// tempo search range and onset-envelope history for autocorrelation
#define MIN_BPM       60.0f
#define MAX_BPM       180.0f
#define ONSET_HIST    256      // ~8s of onset strength
#define FLUX_HI_BIN   64       // spectral flux over bins 1..64 (~2kHz), where beats live

static I2SClass i2s;

static int32_t rawSamples[FFT_SAMPLES];
static float vReal[FFT_SAMPLES];
static float vImag[FFT_SAMPLES];
static ArduinoFFT<float> FFT(vReal, vImag, FFT_SAMPLES, SAMPLE_RATE);

static const int BASS_LO = (int)(60.0f / BIN_HZ);
static const int BASS_HI = (int)(250.0f / BIN_HZ);
static const int MID_LO  = (int)(250.0f / BIN_HZ);
static const int MID_HI  = (int)(2000.0f / BIN_HZ);
static const int TREB_LO = (int)(2000.0f / BIN_HZ);
static const int TREB_HI = (int)(7000.0f / BIN_HZ);

static portMUX_TYPE featMux = portMUX_INITIALIZER_UNLOCKED;
static AudioFeatures features = {};

static uint32_t beatUntilMs = 0;
static uint32_t micPageHeartbeatUntilMs = 0;

static const uint32_t BEAT_FLASH_MS = 120;
static const uint32_t MIC_PAGE_HEARTBEAT_TIMEOUT_MS = 1000;

static AudioSettings settings = { 50, 10, 150, 150 };

AudioSettings& audioSettings() { return settings; }

// --- mic noise-check recording (raw WAV to LittleFS) ------------------------

static File micRecordFile;
static File micMetaFile;
static volatile bool micRecording = false;
static uint32_t micRecordTotalSamples = SAMPLE_RATE * 10;
static uint32_t micRecordSamplesWritten = 0;
static bool micRecordFileReady = false;

static void writeU32LE(uint8_t* buf, uint32_t v) {
  buf[0] = v & 0xFF; buf[1] = (v >> 8) & 0xFF; buf[2] = (v >> 16) & 0xFF; buf[3] = (v >> 24) & 0xFF;
}
static void writeU16LE(uint8_t* buf, uint16_t v) {
  buf[0] = v & 0xFF; buf[1] = (v >> 8) & 0xFF;
}

// standard 44-byte PCM WAV header; dataSize is known upfront since the
// recording length is fixed, so no need to patch it in after the fact
static void writeWavHeader(File& f, uint32_t sampleRate, uint16_t bitsPerSample, uint32_t dataSize) {
  uint8_t h[44];
  memcpy(h, "RIFF", 4);
  writeU32LE(h + 4, 36 + dataSize);
  memcpy(h + 8, "WAVE", 4);
  memcpy(h + 12, "fmt ", 4);
  writeU32LE(h + 16, 16); // PCM fmt chunk size
  writeU16LE(h + 20, 1);  // AudioFormat = PCM
  writeU16LE(h + 22, 1);  // NumChannels = mono
  writeU32LE(h + 24, sampleRate);
  writeU32LE(h + 28, sampleRate * (uint32_t)(bitsPerSample / 8)); // ByteRate
  writeU16LE(h + 32, bitsPerSample / 8);                          // BlockAlign
  writeU16LE(h + 34, bitsPerSample);
  memcpy(h + 36, "data", 4);
  writeU32LE(h + 40, dataSize);
  f.write(h, sizeof(h));
}

void micRecordStart(uint32_t seconds) {
  if (micRecording) return;
  if (seconds < 5) seconds = 5;
  if (seconds > 30) seconds = 30; // flash ceiling: 32KB/s WAV into the 1.5MB partition
  File f = LittleFS.open(MIC_RECORDING_PATH, "w");
  if (!f) return;
  micRecordTotalSamples = SAMPLE_RATE * seconds;
  writeWavHeader(f, SAMPLE_RATE, 16, micRecordTotalSamples * 2);
  micRecordFile = f;

  // metadata sidecar: header comment records the settings in effect, so an
  // offline analysis knows what the pipeline was configured to do
  micMetaFile = LittleFS.open(MIC_META_PATH, "w");
  if (micMetaFile) {
    char hdr[192];
    int n = snprintf(hdr, sizeof(hdr),
      "# gain=%u noiseFloor=%u beatThreshold=%u debounceMs=%u sampleRate=%d fftSamples=%d fps=%.2f seconds=%lu\n"
      "ms,flux,fluxMean,onset,bass,mid,treb,vol,beat,bpm,conf,phase\n",
      settings.gain, settings.noiseFloor, settings.beatThreshold, settings.beatDebounceMs,
      SAMPLE_RATE, FFT_SAMPLES, (double)FPS, (unsigned long)seconds);
    if (n > 0) micMetaFile.write((const uint8_t*)hdr, n);
  }

  micRecordSamplesWritten = 0;
  micRecordFileReady = false;
  micRecording = true;
}

bool micRecordInProgress() { return micRecording; }
float micRecordProgress() { return micRecording ? (float)micRecordSamplesWritten / (float)micRecordTotalSamples : 0.0f; }
bool micRecordReady() { return micRecordFileReady; }

void audioSaveSettings() {
  File f = LittleFS.open(SETTINGS_FILE, "w");
  if (!f) return;
  f.write((const uint8_t*)&settings, sizeof(settings));
  f.close();
}

static void loadSettings() {
  File f = LittleFS.open(SETTINGS_FILE, "r");
  if (!f) return;
  if (f.size() == sizeof(settings)) f.read((uint8_t*)&settings, sizeof(settings));
  f.close();
}

// --- DSP task state (core 0 only) -------------------------------------------

static float bassPeak = 1e-6f, midPeak = 1e-6f, trebPeak = 1e-6f;
static float bassAvg = 0.0f;
static uint32_t lastBeatMs = 0;

static float prevMag[FFT_SAMPLES / 2]; // log-compressed magnitudes of the previous frame
static float onsetHist[ONSET_HIST];    // conditioned onset strength (local-mean removed, rectified)
static int onsetHead = 0;
static float fluxMean = 0.0f;          // short local mean of the flux (the slow-varying baseline)
static uint32_t frameCounter = 0;

// tempo / PLL
static float smoothedBpm = 0.0f;
static float tempoScale = 1.0f;   // x0.5 / x1 / x2 nudge
static float phase = 0.0f;        // 0..1 within a beat
static uint32_t beatCount = 0;
static float confidence = 0.0f;

// tap tempo
static uint32_t tapTimes[4] = {0};
static uint8_t tapIdx = 0;
static float overrideBpm = 0.0f;
static uint32_t overrideUntilMs = 0;

static float bandEnergy(int lo, int hi) {
  if (lo < 1) lo = 1;
  int half = FFT_SAMPLES / 2;
  if (hi > half) hi = half;
  float sum = 0;
  for (int i = lo; i < hi; i++) sum += vReal[i];
  int n = hi - lo;
  return n > 0 ? sum / n : 0.0f;
}

static inline float onsetAt(int k) { // k=0 is the newest sample
  int idx = ((onsetHead - 1 - k) % ONSET_HIST + ONSET_HIST) % ONSET_HIST;
  return onsetHist[idx];
}

// scratch for the normalized autocorrelation (file-static rather than on
// the DSP task's stack)
static float acf[ONSET_HIST];
static uint8_t lowConfCount = 0;

// No credible periodicity right now: hold the last tempo briefly (a quiet
// bar or breakdown shouldn't drop the lock instantly), then stop reporting
// a tempo at all rather than keep showing a stale/imagined one.
static void tempoLost() {
  if (lowConfCount < 255) lowConfCount++;
  if (lowConfCount >= 16) smoothedBpm = 0.0f; // ~4s at 4 updates/s
}

// Zero-mean, variance-normalized autocorrelation of the onset envelope ->
// tempo estimate. The mean removal is load-bearing: the envelope is
// all-positive, and autocorrelating it with its DC left in gives a
// near-flat ACF (every lag scores ~mean^2) - that flatness is what capped
// confidence around 25% no matter how strong the beat was, and let the old
// narrow 120-BPM prior pick a phantom ~110 BPM out of silence. On the
// zero-mean signal r is a true correlation in [-1,1]: ~0 for noise and
// silence, high only for real periodicity - so the peak's r value IS the
// confidence, and doubles as the silence gate.
static void computeTempo() {
  int minLag = (int)roundf(FPS * 60.0f / MAX_BPM);
  int maxLag = (int)roundf(FPS * 60.0f / MIN_BPM);
  if (minLag < 2) minLag = 2;
  if (maxLag > (ONSET_HIST - 1) / 2) maxLag = (ONSET_HIST - 1) / 2; // harmonic scoring reads r at 2*lag

  float mu = 0;
  for (int i = 0; i < ONSET_HIST; i++) mu += onsetHist[i];
  mu /= ONSET_HIST;
  float var = 0;
  for (int i = 0; i < ONSET_HIST; i++) { float d = onsetHist[i] - mu; var += d * d; }
  var /= ONSET_HIST;
  if (var < 1e-12f) { // dead-flat envelope (true silence)
    confidence = 0.0f;
    tempoLost();
    return;
  }

  int lo = minLag - 1;      // one extra lag each side for the peak interpolation below
  int hi = maxLag * 2 + 1;  // through 2*lag for harmonic scoring
  if (lo < 1) lo = 1;
  if (hi > ONSET_HIST - 1) hi = ONSET_HIST - 1;
  for (int lag = lo; lag <= hi; lag++) {
    float s = 0;
    for (int i = lag; i < ONSET_HIST; i++) s += (onsetAt(i) - mu) * (onsetAt(i - lag) - mu);
    acf[lag] = s / ((ONSET_HIST - lag) * var);
  }

  // Pick the beat period: correlation at the lag plus support from its
  // double - the true tempo also correlates at 2 beats, so it outscores
  // half-time impostors on evidence. The tempo prior stays, but wide
  // (sigma = 1 octave, was 0.6): a tie-breaker, not a dictator - the old
  // narrow prior actively fought legitimate fast tempi like 160 BPM.
  float best = -1e9f;
  int bestLag = 0;
  for (int lag = minLag; lag <= maxLag; lag++) {
    float score = acf[lag] + 0.5f * acf[lag * 2];
    float lg = log2f((FPS * 60.0f / lag) / 120.0f);
    score *= expf(-0.5f * lg * lg);
    if (score > best) { best = score; bestLag = lag; }
  }
  if (bestLag <= 0) {
    confidence = 0.0f;
    tempoLost();
    return;
  }

  // Parabolic interpolation around the peak for a fractional beat period.
  // At ~31 envelope frames/sec the integer lags near fast tempi are >10 BPM
  // apart (160 BPM = lag 11.7 - there is no integer bin for it, its energy
  // splits across lags 11 and 12), which is why fast beats couldn't be
  // followed; the quadratic fit recovers the in-between period.
  float y0 = acf[bestLag - 1], y1 = acf[bestLag], y2 = acf[bestLag + 1];
  float denom = y0 - 2.0f * y1 + y2;
  float frac = (fabsf(denom) > 1e-9f) ? 0.5f * (y0 - y2) / denom : 0.0f;
  if (frac > 0.5f) frac = 0.5f;
  if (frac < -0.5f) frac = -0.5f;
  float peakR = y1 - 0.25f * (y0 - y2) * frac; // interpolated peak height

  float bpm = (FPS * 60.0f / ((float)bestLag + frac)) * tempoScale;
  if (bpm < MIN_BPM) bpm *= 2.0f;
  if (bpm > MAX_BPM) bpm *= 0.5f;

  confidence = peakR < 0.0f ? 0.0f : (peakR > 1.0f ? 1.0f : peakR);

  if (confidence < 0.15f) { // noise floor for a real correlation peak
    tempoLost();
    return; // don't blend a garbage estimate into the tempo
  }
  lowConfCount = 0;

  if (smoothedBpm < 1e-3f) {
    smoothedBpm = bpm;
  } else {
    // Once confidently locked, blend new estimates in much more slowly -
    // the tempo should settle and hold steady rather than wander with
    // every wobble in the onset envelope. While unconfident, re-acquire
    // quickly.
    float alpha = confidence > 0.5f ? 0.05f : 0.2f;
    smoothedBpm = smoothedBpm * (1.0f - alpha) + bpm * alpha;
  }
}

static void processFrame(uint32_t nowMs) {
  float mean = 0;
  for (int i = 0; i < FFT_SAMPLES; i++) {
    vReal[i] = (float)(rawSamples[i] >> 8);
    mean += vReal[i];
  }
  mean /= FFT_SAMPLES;
  for (int i = 0; i < FFT_SAMPLES; i++) { vReal[i] -= mean; vImag[i] = 0.0f; }

  FFT.windowing(FFTWindow::Hann, FFTDirection::Forward);
  FFT.compute(FFTDirection::Forward);
  FFT.complexToMagnitude();

  float gain = powf(2.0f, (float)(settings.gain - 1) / 14.0f);
  float scale = gain / 8388608.0f;

  float bassE = bandEnergy(BASS_LO, BASS_HI) * scale;
  float midE  = bandEnergy(MID_LO, MID_HI) * scale;
  float trebE = bandEnergy(TREB_LO, TREB_HI) * scale;

  bassPeak = fmaxf(bassPeak * 0.999f, bassE);
  midPeak  = fmaxf(midPeak * 0.999f, midE);
  trebPeak = fmaxf(trebPeak * 0.999f, trebE);
  if (bassPeak < 1e-6f) bassPeak = 1e-6f;
  if (midPeak < 1e-6f) midPeak = 1e-6f;
  if (trebPeak < 1e-6f) trebPeak = 1e-6f;

  float noise = settings.noiseFloor / 100.0f * 0.1f;
  float bassN = bassE > noise ? fminf(bassE / bassPeak, 1.0f) : 0.0f;
  float midN  = midE > noise ? fminf(midE / midPeak, 1.0f) : 0.0f;
  float trebN = trebE > noise ? fminf(trebE / trebPeak, 1.0f) : 0.0f;
  float volume = fminf((bassN + midN + trebN) / 3.0f, 1.0f);

  // Log-compressed spectral flux (sum of positive changes in log-magnitude)
  // -> onset strength. Log compression is the standard conditioning: a
  // doubling of a bin's magnitude contributes the same amount whether the
  // music is quiet or loud, so the envelope doesn't need an AGC.
  float flux = 0;
  for (int i = 1; i < FLUX_HI_BIN; i++) {
    float m = log1pf(vReal[i] * scale * 10.0f);
    float d = m - prevMag[i];
    if (d > 0) flux += d;
    prevMag[i] = m;
  }
  // Subtract a short local mean and half-wave rectify, so the stored
  // envelope is spiky at onsets and ~zero elsewhere. (Replaces the old
  // decaying-peak normalizer, which in a silent room stretched mic
  // self-noise to full scale and handed the tempo tracker a fake signal -
  // the source of the phantom ~110 BPM readings in silence.)
  fluxMean = fluxMean * 0.95f + flux * 0.05f; // ~0.6s time constant at 31 fps
  float onset = flux - fluxMean;
  onsetHist[onsetHead] = onset > 0.0f ? onset : 0.0f;
  onsetHead = (onsetHead + 1) % ONSET_HIST;

  int peakBin = 1;
  float peakMag = 0;
  int half = FFT_SAMPLES / 2;
  for (int i = 1; i < half; i++) if (vReal[i] > peakMag) { peakMag = vReal[i]; peakBin = i; }
  float dominantHz = peakBin * BIN_HZ;

  // effective tempo: tap override wins briefly, else the detected tempo
  float bpm = (nowMs < overrideUntilMs && overrideBpm > 0) ? overrideBpm : smoothedBpm;

  // immediate onset flag (bass spike over baseline), also nudges the PLL phase
  if (bassAvg < 1e-9f) bassAvg = bassN;
  float threshold = settings.beatThreshold / 100.0f;
  // debounce, capped at ~60% of the tracked beat period so a large setting
  // can't choke fast tempi (at 160 BPM beats are only 375ms apart)
  uint32_t debounceMs = settings.beatDebounceMs;
  if (bpm > 1.0f) {
    uint32_t cap = (uint32_t)(36000.0f / bpm); // 0.6 * 60000 / bpm
    if (debounceMs > cap) debounceMs = cap;
  }
  bool debounceOk = (nowMs - lastBeatMs) >= debounceMs;
  bool isBeat = false;
  if (debounceOk && bassN > noise && bassN > bassAvg * threshold && bassN > 0.15f) {
    isBeat = true;
    lastBeatMs = nowMs;
    beatUntilMs = nowMs + BEAT_FLASH_MS;
  }
  bassAvg = bassAvg * 0.95f + bassN * 0.05f;

  // update tempo estimate a few times per second (autocorrelation is cheap but not free)
  frameCounter++;
  if ((frameCounter % 8) == 0) computeTempo();

  // PLL beat clock: advance phase; a detected onset pulls it toward the beat
  if (bpm > 1.0f) {
    float periodFrames = FPS * 60.0f / bpm;
    phase += 1.0f / periodFrames;
    if (isBeat) {
      float err = phase; if (err > 0.5f) err -= 1.0f;
      phase -= 0.10f * err;
    }
    while (phase >= 1.0f) { phase -= 1.0f; beatCount++; }
    while (phase < 0.0f) { phase += 1.0f; }
  }
  float barPhase = ((beatCount % 4) + phase) / 4.0f;

  portENTER_CRITICAL(&featMux);
  features.bass = bassN;
  features.mid = midN;
  features.treble = trebN;
  features.volume = volume;
  features.beat = isBeat;
  features.bpm = bpm;
  features.beatPhase = phase;
  features.barPhase = barPhase;
  features.confidence = confidence;
  features.dominantHz = dominantHz;
  features.beatCount = beatCount;
  features.frameMs = nowMs;
  portEXIT_CRITICAL(&featMux);

  // one metadata row per frame while a noise-check recording is running -
  // the pipeline's view of the exact audio going into the WAV
  if (micRecording && micMetaFile) {
    char row[128];
    int n = snprintf(row, sizeof(row), "%lu,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%d,%.1f,%.3f,%.3f\n",
      (unsigned long)nowMs, (double)flux, (double)fluxMean,
      (double)(onset > 0.0f ? onset : 0.0f),
      (double)bassN, (double)midN, (double)trebN, (double)volume,
      isBeat ? 1 : 0, (double)bpm, (double)confidence, (double)phase);
    if (n > 0) micMetaFile.write((const uint8_t*)row, n);
  }
}

// Same 24-bit-in-32-bit sample layout processFrame() uses (rawSamples[i] >> 8
// is the true 24-bit signed value) - shift 8 further down to 16-bit for the
// WAV file, trading the bottom 8 bits of precision for a widely-playable format.
static void writeRecordingFrame() {
  int16_t buf16[FFT_SAMPLES];
  for (int i = 0; i < FFT_SAMPLES; i++) buf16[i] = (int16_t)(rawSamples[i] >> 16);

  size_t toWrite = FFT_SAMPLES;
  if (micRecordSamplesWritten + FFT_SAMPLES > micRecordTotalSamples) {
    toWrite = micRecordTotalSamples - micRecordSamplesWritten;
  }
  if (toWrite > 0) micRecordFile.write((const uint8_t*)buf16, toWrite * sizeof(int16_t));
  micRecordSamplesWritten += toWrite;

  if (micRecordSamplesWritten >= micRecordTotalSamples) {
    micRecordFile.close();
    if (micMetaFile) {
      // final autocorrelation snapshot (most recent computeTempo result),
      // so the offline analysis can compare its own ACF to the device's
      micMetaFile.print("#ACF\nlag,bpm,r\n");
      int minLag = (int)roundf(FPS * 60.0f / MAX_BPM);
      int maxLag = (int)roundf(FPS * 60.0f / MIN_BPM);
      if (minLag < 2) minLag = 2;
      if (maxLag > (ONSET_HIST - 1) / 2) maxLag = (ONSET_HIST - 1) / 2;
      int lo = minLag - 1;
      int hi = maxLag * 2 + 1;
      if (lo < 1) lo = 1;
      if (hi > ONSET_HIST - 1) hi = ONSET_HIST - 1;
      char row[48];
      for (int lag = lo; lag <= hi; lag++) {
        int n = snprintf(row, sizeof(row), "%d,%.1f,%.4f\n", lag, (double)(FPS * 60.0f / lag), (double)acf[lag]);
        if (n > 0) micMetaFile.write((const uint8_t*)row, n);
      }
      micMetaFile.close();
    }
    micRecording = false;
    micRecordFileReady = true;
  }
}

static void dspTask(void*) {
  for (;;) {
    size_t got = i2s.readBytes((char*)rawSamples, sizeof(rawSamples));
    if (got < sizeof(rawSamples)) { vTaskDelay(1); continue; }
    if (micRecording) writeRecordingFrame();
    processFrame(millis());
  }
}

void audioInit() {
  LittleFS.begin(true);
  loadSettings();
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, HIGH); // off (active low)
  i2s.setPins(MIC_SCK_PIN, MIC_WS_PIN, -1, MIC_SD_PIN);
  i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
  xTaskCreatePinnedToCore(dspTask, "dsp", 8192, nullptr, 1, nullptr, 0);
}

// The DSP itself runs on the core-0 task; this (called every loop()) just
// drives the onboard status LED as a real-time beat indicator - no UI
// polling latency. Once a tempo is tracked it blinks with the PLL beat
// clock (the same clock beat-synced effects follow, so LED vs. LEDs vs.
// music can be compared directly); before that, it flashes raw detected
// onsets so there's still feedback while the tempo settles.
void audioUpdate(uint32_t nowMs) {
  static float lastPhase = 0.0f;
  static uint32_t flashUntilMs = 0;

  AudioFeatures f = audioFeatures();
  if (f.bpm > 1.0f) {
    if (f.beatPhase < lastPhase) flashUntilMs = nowMs + 60; // phase wrapped -> a beat just started
    lastPhase = f.beatPhase;
  } else if (audioBeatActive()) {
    flashUntilMs = nowMs + 60;
  }
  digitalWrite(STATUS_LED_PIN, (nowMs < flashUntilMs) ? LOW : HIGH); // active low
}

AudioFeatures audioFeatures() {
  AudioFeatures snap;
  portENTER_CRITICAL(&featMux);
  snap = features;
  portEXIT_CRITICAL(&featMux);
  return snap;
}

void audioTap() {
  uint32_t now = millis();
  tapTimes[tapIdx % 4] = now;
  tapIdx++;
  // average the gaps between the last few taps (ignore stale gaps > 2s)
  float sum = 0;
  int n = 0;
  for (int i = 1; i < 4 && i < (int)tapIdx; i++) {
    uint32_t a = tapTimes[(tapIdx - i) % 4];
    uint32_t b = tapTimes[(tapIdx - i - 1) % 4];
    if (a > b && (a - b) < 2000) { sum += (a - b); n++; }
  }
  if (n > 0) {
    float bpm = 60000.0f / (sum / n);
    if (bpm >= MIN_BPM && bpm <= MAX_BPM) {
      overrideBpm = bpm;
      overrideUntilMs = now + 8000; // hold the tapped tempo for 8s
      smoothedBpm = bpm;
      phase = 0.0f;                 // align the downbeat to this tap
      confidence = 1.0f;
    }
  }
}

void audioTempoNudge(int dir) {
  if (dir > 0) tempoScale = fminf(tempoScale * 2.0f, 2.0f);
  else tempoScale = fmaxf(tempoScale * 0.5f, 0.5f);
  smoothedBpm = 0.0f; // let it re-lock at the new scale
}

float audioLevel() { return audioFeatures().volume; }
bool audioBeatActive() { return millis() < beatUntilMs; }
void audioMarkMicPageActive(uint32_t nowMs) { micPageHeartbeatUntilMs = nowMs + MIC_PAGE_HEARTBEAT_TIMEOUT_MS; }
bool audioCalibrationActive() { return millis() < micPageHeartbeatUntilMs; }
