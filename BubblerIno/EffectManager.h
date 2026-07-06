#pragma once
#include <Arduino.h>

enum EffectId {
  EFFECT_OFF = 0,
  EFFECT_VERTICAL_SWEEP,
  EFFECT_COUNT
};

extern const char* const EFFECT_NAMES[EFFECT_COUNT];

void setActiveEffect(EffectId id);
EffectId getActiveEffect();

// call every loop() iteration; runs whichever effect is active and shows the frame
void runActiveEffect(uint32_t nowMs);
