#include "EffectManager.h"
#include "Effects.h"
#include "Rings.h"
#include "Palette.h"

const char* const EFFECT_NAMES[EFFECT_COUNT] = {
  "Off",
  "Vertical Sweep",
};

static EffectId activeEffect = EFFECT_OFF;
static const Palette sweepPalette = { { CRGB::Red, CRGB::Green, CRGB::Blue }, 3 };

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

void runActiveEffect(uint32_t nowMs) {
  switch (activeEffect) {
    case EFFECT_VERTICAL_SWEEP:
      effectVerticalSweep(sweepPalette, 1000, nowMs);
      showAll();
      break;
    case EFFECT_OFF:
    default:
      break;
  }
}
