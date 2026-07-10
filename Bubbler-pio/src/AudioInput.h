#pragma once
#include <Arduino.h>

struct AudioSettings {
  uint8_t gain;           // 1-100, mic sensitivity (log-scaled internally into a gain multiplier)
  uint8_t noiseFloor;     // 0-100, level below this is squelched to silence (noise gate)
  uint16_t beatThreshold; // 110-400, % above the running baseline that counts as a beat
  uint16_t beatDebounceMs; // 30-500, minimum time between consecutive beat triggers
};

// Snapshot of the live audio analysis, produced by the core-0 DSP task and
// read lock-free by effects/UI via audioFeatures(). Band values are 0..1
// (AGC-normalized). bpm/beatPhase/barPhase/confidence are filled in a later
// step (tempo tracking) and are 0 until then.
struct AudioFeatures {
  float bass;        // 0..1 low-band energy
  float mid;         // 0..1 mid-band energy
  float treble;      // 0..1 high-band energy
  float volume;      // 0..1 smoothed overall level
  bool  beat;        // true briefly on a detected onset/beat
  float bpm;         // detected tempo (0 = unknown yet)
  float beatPhase;   // 0..1 position within the current beat
  float barPhase;    // 0..1 position within a 4-beat bar
  float confidence;  // 0..1 tempo-lock confidence
  float dominantHz;  // frequency of the loudest FFT bin
  uint32_t beatCount; // beats elapsed since boot per the PLL clock; beatCount
                      // + beatPhase is a continuous musical position in beats
  uint32_t frameMs;   // millis() when this snapshot was produced (~32/s), so
                      // consumers can extrapolate beatPhase between frames
};

AudioSettings& audioSettings();

// persists the current AudioSettings to flash so they survive a reboot
void audioSaveSettings();

void audioInit();

// retained for source compatibility; the DSP now runs on its own core-0 task,
// so this is a no-op. Safe to keep calling from loop().
void audioUpdate(uint32_t nowMs);

AudioFeatures audioFeatures(); // thread-safe snapshot copy

// tempo overrides (wired to the Mic page in a later step)
void audioTap();              // register a tap; sets BPM from tap intervals
void audioTempoNudge(int dir); // dir>0 doubles, dir<0 halves the detected tempo

float audioLevel();     // 0.0-1.0, smoothed amplitude for a VU meter
bool audioBeatActive();  // true briefly right after a detected beat/onset

// call whenever the mic settings page's live poll hits the server
void audioMarkMicPageActive(uint32_t nowMs);

// true while the mic settings page appears to be open (polled recently);
// used to flash the physical LEDs with the beat for on-device calibration
bool audioCalibrationActive();

// Raw mic noise-check recording: captures a fixed ~10s of mono 16-bit PCM
// straight from the I2S mic to a WAV file on LittleFS, for download over
// WiFi (too big to push over the BLE chunk protocol at any real speed).
#define MIC_RECORDING_PATH "/mic_recording.wav"
void micRecordStart();          // (re)starts the recording; no-op if one is already in progress
bool micRecordInProgress();
float micRecordProgress();      // 0..1 while recording
bool micRecordReady();          // a finished recording exists at MIC_RECORDING_PATH
