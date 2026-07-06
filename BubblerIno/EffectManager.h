#pragma once
#include <Arduino.h>
#include "EffectParams.h"

enum EffectId {
  EFFECT_OFF = 0,
  EFFECT_VERTICAL_SWEEP,
  EFFECT_HORIZONTAL_SWEEP,
  EFFECT_ALTERNATE_FLASH,
  EFFECT_SPIRAL,
  EFFECT_SNOW,
  EFFECT_COUNT
};

extern const char* const EFFECT_NAMES[EFFECT_COUNT];

void setActiveEffect(EffectId id);
EffectId getActiveEffect();

// each effect remembers its own params across activations
EffectParams& effectParamsFor(EffectId id);

// call every loop() iteration; runs whichever effect is active and shows the frame
void runActiveEffect(uint32_t nowMs);
