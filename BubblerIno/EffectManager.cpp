#include "EffectManager.h"
#include "Effects.h"
#include "Rings.h"

const char* const EFFECT_NAMES[EFFECT_COUNT] = {
  "Off",
  "Vertical Sweep",
  "Horizontal Sweep",
  "Alternate Flash",
  "Spiral",
  "Snow",
};

static EffectId activeEffect = EFFECT_OFF;

static EffectParams effectParams[EFFECT_COUNT] = {
  { { {}, 0 }, 50, DIR_FORWARD, 1 },                                      // Off (unused)
  { { { CRGB::Red, CRGB::Green, CRGB::Blue }, 3 }, 50, DIR_FORWARD, 3 },   // Vertical Sweep
  { { { CRGB::Red, CRGB::Green, CRGB::Blue }, 3 }, 50, DIR_FORWARD, 3 },   // Horizontal Sweep
  { { { CRGB::Red, CRGB::Blue }, 2 }, 50, DIR_FORWARD, 1 },                // Alternate Flash
  { { { CRGB::Red, CRGB::Green, CRGB::Blue }, 3 }, 50, DIR_FORWARD, 2 },   // Spiral
  { { { CRGB::White }, 1 }, 50, DIR_FORWARD, 1 },                         // Snow
};

void setActiveEffect(EffectId id) {
  activeEffect = id;
  if (id == EFFECT_OFF) {
    setAll(CRGB::Black);
    showAll();
  }
}

EffectId getActiveEffect() {
  return activeEffect;
}

EffectParams& effectParamsFor(EffectId id) {
  return effectParams[id];
}

void runActiveEffect(uint32_t nowMs) {
  const EffectParams& p = effectParams[activeEffect];
  switch (activeEffect) {
    case EFFECT_VERTICAL_SWEEP:   effectVerticalSweep(p, nowMs); break;
    case EFFECT_HORIZONTAL_SWEEP: effectHorizontalSweep(p, nowMs); break;
    case EFFECT_ALTERNATE_FLASH:  effectAlternateFlash(p, nowMs); break;
    case EFFECT_SPIRAL:           effectSpiral(p, nowMs); break;
    case EFFECT_SNOW:             effectSnow(p, nowMs); break;
    case EFFECT_OFF:
    default:
      return; // already blacked out by setActiveEffect
  }
  showAll();
}
