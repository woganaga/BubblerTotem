#pragma once
#include <Arduino.h>

struct AudioSettings {
  uint8_t gain;           // 1-100, mic sensitivity (log-scaled internally into a gain multiplier)
  uint8_t noiseFloor;     // 0-100, level below this is squelched to silence (noise gate)
  uint16_t beatThreshold; // 110-400, % above the running baseline that counts as a beat
  uint16_t beatDebounceMs; // 30-500, minimum time between consecutive beat triggers
};

AudioSettings& audioSettings();

// persists the current AudioSettings to flash so they survive a reboot
void audioSaveSettings();

void audioInit();

// call every loop() iteration; non-blocking, only does work once a full
// sample chunk has arrived
void audioUpdate(uint32_t nowMs);

float audioLevel();     // 0.0-1.0, smoothed amplitude for a VU meter
bool audioBeatActive();  // true briefly right after a detected beat/onset

// call whenever the mic settings page's live poll hits the server
void audioMarkMicPageActive(uint32_t nowMs);

// true while the mic settings page appears to be open (polled recently);
// used to flash the physical LEDs with the beat for on-device calibration
bool audioCalibrationActive();
