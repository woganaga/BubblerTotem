#include "AudioInput.h"
#include "ESP_I2S.h"
#include "LittleFS.h"

static const char* SETTINGS_FILE = "/mic_settings.bin";

#define MIC_SCK_PIN  44
#define MIC_WS_PIN   7
#define MIC_SD_PIN   8

static I2SClass i2s;

static const size_t SAMPLE_BUF_WORDS = 128;
static int32_t sampleBuf[SAMPLE_BUF_WORDS];

static float currentLevel = 0.0f;   // smoothed 0..1 level for the VU meter
static float runningAverage = 0.0f; // slow-moving baseline used for beat detection
static uint32_t beatUntilMs = 0;
static uint32_t lastBeatMs = 0;
static uint32_t lastNowMs = 0;
static uint32_t micPageHeartbeatUntilMs = 0;

static const uint32_t BEAT_FLASH_MS = 120;
static const uint32_t MIC_PAGE_HEARTBEAT_TIMEOUT_MS = 1000; // page polls every 150ms; comfortably covers normal gaps

static AudioSettings settings = {
  50,   // gain
  10,   // noiseFloor
  150,  // beatThreshold (150% of baseline)
  150,  // beatDebounceMs
};

AudioSettings& audioSettings() {
  return settings;
}

void audioSaveSettings() {
  File f = LittleFS.open(SETTINGS_FILE, "w");
  if (!f) return;
  f.write((const uint8_t*)&settings, sizeof(settings));
  f.close();
}

static void loadSettings() {
  File f = LittleFS.open(SETTINGS_FILE, "r");
  if (!f) return;
  if (f.size() == sizeof(settings)) {
    f.read((uint8_t*)&settings, sizeof(settings));
  }
  f.close();
}

void audioInit() {
  LittleFS.begin(true); // format on first use
  loadSettings();

  i2s.setPins(MIC_SCK_PIN, MIC_WS_PIN, -1, MIC_SD_PIN);
  i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
}

void audioUpdate(uint32_t nowMs) {
  lastNowMs = nowMs;

  if (i2s.available() < (int)sizeof(sampleBuf)) return; // not enough new audio yet, don't block waiting for it

  size_t bytesRead = i2s.readBytes((char*)sampleBuf, sizeof(sampleBuf));
  size_t samples = bytesRead / sizeof(int32_t);
  if (samples == 0) return;

  // INMP441 left-justifies its 24 significant bits within the 32-bit word
  int64_t sumAbs = 0;
  for (size_t i = 0; i < samples; i++) {
    int32_t sample = sampleBuf[i] >> 8;
    sumAbs += abs(sample);
  }
  float avgAbs = (float)sumAbs / samples;

  // log-scaled gain: 1x at gain=1 up to ~130x at gain=100, so the slider feels
  // even across its whole range rather than being all bunched up at one end
  float gainMultiplier = powf(2.0f, (float)(settings.gain - 1) / 14.0f);
  float level = (avgAbs / 8388608.0f) * gainMultiplier;

  float noiseFloorNorm = settings.noiseFloor / 100.0f * 0.3f; // 0-100 -> 0-0.3 of full scale
  if (level < noiseFloorNorm) level = 0.0f;
  if (level > 1.0f) level = 1.0f;

  currentLevel = currentLevel * 0.7f + level * 0.3f; // smooth for display

  if (runningAverage < 0.0001f) runningAverage = currentLevel; // seed on first read

  float thresholdRatio = settings.beatThreshold / 100.0f;
  bool debounceElapsed = (nowMs - lastBeatMs) >= settings.beatDebounceMs;
  if (debounceElapsed && currentLevel > noiseFloorNorm && currentLevel > runningAverage * thresholdRatio) {
    beatUntilMs = nowMs + BEAT_FLASH_MS;
    lastBeatMs = nowMs;
  }
  runningAverage = runningAverage * 0.98f + currentLevel * 0.02f;
}

float audioLevel() {
  return currentLevel;
}

bool audioBeatActive() {
  return lastNowMs < beatUntilMs;
}

void audioMarkMicPageActive(uint32_t nowMs) {
  micPageHeartbeatUntilMs = nowMs + MIC_PAGE_HEARTBEAT_TIMEOUT_MS;
}

bool audioCalibrationActive() {
  return lastNowMs < micPageHeartbeatUntilMs;
}
