#pragma once
#include <Arduino.h>
#include "EffectParams.h"

enum EffectId {
  EFFECT_OFF = 0,
  EFFECT_VERTICAL_SWEEP,
  EFFECT_HORIZONTAL_SWEEP,
  EFFECT_ALTERNATE_FLASH,
  EFFECT_CHASE,
  EFFECT_SPIRAL,
  EFFECT_SNOW,
  EFFECT_PINWHEEL,
  EFFECT_COLORWASH,
  EFFECT_FIRE,
  EFFECT_CONFETTI,
  EFFECT_RIPPLE,
  // xLights-derived effects (XLFX.*)
  EFFECT_XL_BARS,
  EFFECT_XL_COLORWASH,
  EFFECT_XL_SPIRALS,
  EFFECT_XL_PINWHEEL,
  EFFECT_XL_BUTTERFLY,
  EFFECT_XL_PLASMA,
  EFFECT_XL_SINGLESTRAND,
  EFFECT_XL_MORPH,
  EFFECT_COUNT
};

extern const char* const EFFECT_NAMES[EFFECT_COUNT];

void setActiveEffect(EffectId id);
EffectId getActiveEffect();

// each effect remembers its own params across activations
EffectParams& effectParamsFor(EffectId id);

// call every loop() iteration; runs whichever effect is active and shows the frame
void runActiveEffect(uint32_t nowMs);
