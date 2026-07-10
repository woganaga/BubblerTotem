#include "AudioInput.h"
#include "ESP_I2S.h"
#include "LittleFS.h"
#include <arduinoFFT.h>

static const char* SETTINGS_FILE = "/mic_settings.bin";

#define MIC_SCK_PIN  44
#define MIC_WS_PIN   7
#define MIC_SD_PIN   8

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

static const uint32_t MIC_RECORD_SECONDS = 10;
static const uint32_t MIC_RECORD_TOTAL_SAMPLES = SAMPLE_RATE * MIC_RECORD_SECONDS;

static File micRecordFile;
static volatile bool micRecording = false;
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

void micRecordStart() {
  if (micRecording) return;
  File f = LittleFS.open(MIC_RECORDING_PATH, "w");
  if (!f) return;
  writeWavHeader(f, SAMPLE_RATE, 16, MIC_RECORD_TOTAL_SAMPLES * 2);
  micRecordFile = f;
  micRecordSamplesWritten = 0;
  micRecordFileReady = false;
  micRecording = true;
}

bool micRecordInProgress() { return micRecording; }
float micRecordProgress() { return micRecording ? (float)micRecordSamplesWritten / (float)MIC_RECORD_TOTAL_SAMPLES : 0.0f; }
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

static float prevMag[FFT_SAMPLES / 2];
static float onsetHist[ONSET_HIST];
static int onsetHead = 0;
static float fluxPeak = 1e-6f;
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

// Weighted autocorrelation of the onset envelope -> tempo estimate.
static void computeTempo() {
  int minLag = (int)roundf(FPS * 60.0f / MAX_BPM);
  int maxLag = (int)roundf(FPS * 60.0f / MIN_BPM);
  if (minLag < 2) minLag = 2;
  if (maxLag > ONSET_HIST - 1) maxLag = ONSET_HIST - 1;

  float best = 0, scoreSum = 0;
  int bestLag = 0, scoreCnt = 0;
  for (int lag = minLag; lag <= maxLag; lag++) {
    float s = 0;
    for (int i = lag; i < ONSET_HIST; i++) s += onsetAt(i) * onsetAt(i - lag);
    s /= (ONSET_HIST - lag);
    // bias toward musical tempi (~120 BPM) to fight half/double-time errors
    float bpmAtLag = FPS * 60.0f / lag;
    float lg = log2f(bpmAtLag / 120.0f) / 0.6f;
    s *= expf(-0.5f * lg * lg);
    scoreSum += s;
    scoreCnt++;
    if (s > best) { best = s; bestLag = lag; }
  }
  if (bestLag <= 0) return;

  float bpm = (FPS * 60.0f / bestLag) * tempoScale;
  if (bpm < MIN_BPM) bpm *= 2.0f;
  if (bpm > MAX_BPM) bpm *= 0.5f;
  float avg = scoreCnt ? scoreSum / scoreCnt : 1e-6f;
  float conf = avg > 1e-9f ? (best / avg - 1.0f) / 3.0f : 0.0f;
  confidence = conf < 0 ? 0 : (conf > 1 ? 1 : conf);

  if (smoothedBpm < 1e-3f) {
    smoothedBpm = bpm;
  } else {
    // Once the autocorrelation is confidently locked, blend new estimates in
    // much more slowly - the tempo should settle and hold steady rather than
    // wander with every wobble in the onset envelope. While unconfident,
    // re-acquire quickly.
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

  // spectral flux (sum of positive magnitude changes) -> onset strength envelope
  float flux = 0;
  for (int i = 1; i < FLUX_HI_BIN; i++) {
    float d = vReal[i] - prevMag[i];
    if (d > 0) flux += d;
    prevMag[i] = vReal[i];
  }
  flux *= scale;
  fluxPeak = fmaxf(fluxPeak * 0.999f, flux);
  if (fluxPeak < 1e-6f) fluxPeak = 1e-6f;
  onsetHist[onsetHead] = fminf(flux / fluxPeak, 1.0f);
  onsetHead = (onsetHead + 1) % ONSET_HIST;

  int peakBin = 1;
  float peakMag = 0;
  int half = FFT_SAMPLES / 2;
  for (int i = 1; i < half; i++) if (vReal[i] > peakMag) { peakMag = vReal[i]; peakBin = i; }
  float dominantHz = peakBin * BIN_HZ;

  // immediate onset flag (bass spike over baseline), also nudges the PLL phase
  if (bassAvg < 1e-9f) bassAvg = bassN;
  float threshold = settings.beatThreshold / 100.0f;
  bool debounceOk = (nowMs - lastBeatMs) >= settings.beatDebounceMs;
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

  // effective tempo: tap override wins briefly, else the detected tempo
  float bpm = (nowMs < overrideUntilMs && overrideBpm > 0) ? overrideBpm : smoothedBpm;

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
}

// Same 24-bit-in-32-bit sample layout processFrame() uses (rawSamples[i] >> 8
// is the true 24-bit signed value) - shift 8 further down to 16-bit for the
// WAV file, trading the bottom 8 bits of precision for a widely-playable format.
static void writeRecordingFrame() {
  int16_t buf16[FFT_SAMPLES];
  for (int i = 0; i < FFT_SAMPLES; i++) buf16[i] = (int16_t)(rawSamples[i] >> 16);

  size_t toWrite = FFT_SAMPLES;
  if (micRecordSamplesWritten + FFT_SAMPLES > MIC_RECORD_TOTAL_SAMPLES) {
    toWrite = MIC_RECORD_TOTAL_SAMPLES - micRecordSamplesWritten;
  }
  if (toWrite > 0) micRecordFile.write((const uint8_t*)buf16, toWrite * sizeof(int16_t));
  micRecordSamplesWritten += toWrite;

  if (micRecordSamplesWritten >= MIC_RECORD_TOTAL_SAMPLES) {
    micRecordFile.close();
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
  i2s.setPins(MIC_SCK_PIN, MIC_WS_PIN, -1, MIC_SD_PIN);
  i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
  xTaskCreatePinnedToCore(dspTask, "dsp", 8192, nullptr, 1, nullptr, 0);
}

void audioUpdate(uint32_t) {} // no-op; DSP runs on the core-0 task

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
