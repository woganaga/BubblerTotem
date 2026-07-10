#include "EffectManager.h"
#include "Effects.h"
#include "XLFX.h"
#include "Rings.h"

const char* const EFFECT_NAMES[EFFECT_COUNT] = {
  "Off",
  "Vertical Sweep",
  "Horizontal Sweep",
  "Alternate Flash",
  "Chase",
  "Spiral",
  "Snow",
  "Pinwheel",
  "Colorwash",
  "Fire",
  "Confetti",
  "Ripple",
  "XL Bars",
  "XL Colorwash",
  "XL Spirals",
  "XL Pinwheel",
  "XL Butterfly",
  "XL Plasma",
  "XL SingleStrand",
  "XL Morph",
};

static EffectId activeEffect = EFFECT_OFF;

static EffectParams effectParams[EFFECT_COUNT] = {
  { { {}, 0 }, 50, 100, DIR_FORWARD, 1, 3, 1, 50, 0 },                                          // Off (unused)
  { { { CRGB::Red, CRGB::Green, CRGB::Blue }, 3 }, 50, 100, DIR_FORWARD, 3, 3, 1, 50, 0 },       // Vertical Sweep
  { { { CRGB::Red, CRGB::Green, CRGB::Blue }, 3 }, 50, 100, DIR_FORWARD, 3, 3, 1, 50, 0 },       // Horizontal Sweep
  { { { CRGB::Red, CRGB::Blue }, 2 }, 50, 100, DIR_FORWARD, 1, 3, 1, 50, 0 },                    // Alternate Flash
  { { { CRGB::Red, CRGB::Green, CRGB::Blue }, 3 }, 50, 100, DIR_FORWARD, 2, 3, 1, 50, 0 },       // Chase
  { { { CRGB::Red, CRGB::Green, CRGB::Blue }, 3 }, 50, 100, DIR_FORWARD, 2, 3, 1, 50, 0 },       // Spiral
  { { { CRGB::White }, 1 }, 50, 100, DIR_FORWARD, 1, 3, 1, 50, 0 },                             // Snow
  { { { CRGB::Red, CRGB::Green, CRGB::Blue }, 3 }, 50, 100, DIR_FORWARD, 1, 3, 1, 50, 0 },       // Pinwheel
  { { { CRGB::Red, CRGB::Purple, CRGB::Blue }, 3 }, 50, 100, DIR_FORWARD, 1, 3, 1, 50, 0 },      // Colorwash
  { { { CRGB::Red, CRGB::Orange, CRGB::Yellow }, 3 }, 50, 100, DIR_FORWARD, 1, 3, 1, 50, 0 },    // Fire
  { { { CRGB::Red, CRGB::Green, CRGB::Blue }, 3 }, 50, 100, DIR_FORWARD, 1, 3, 1, 50, 0 },       // Confetti
  { { { CRGB::Blue, CRGB::Cyan }, 2 }, 50, 100, DIR_FORWARD, 2, 3, 1, 50, 0 },                   // Ripple
  //                                                              w  t  r  ov dc | cnt sty den th   twist fl f2
  { { { CRGB::Red, CRGB::Green, CRGB::Blue }, 3 }, 40, 100, DIR_FORWARD, 1, 3, 1, 50, 0,  2, 1, 1, 0,    0, 1, 0 }, // XL Bars
  { { { CRGB::Red, CRGB::Purple, CRGB::Blue }, 3 }, 30, 100, DIR_FORWARD, 1, 3, 1, 50, 0, 1, 0, 1, 0,   0, 0, 0 }, // XL Colorwash
  { { { CRGB::Red, CRGB::Green, CRGB::Blue }, 3 }, 40, 100, DIR_FORWARD, 1, 3, 1, 50, 0,  1, 0, 1, 50, 120, 1, 0 }, // XL Spirals
  { { { CRGB::Red, CRGB::Green, CRGB::Blue }, 3 }, 40, 100, DIR_FORWARD, 1, 3, 1, 50, 0,  4, 0, 1, 40,  90, 1, 0 }, // XL Pinwheel
  { { { CRGB::Red, CRGB::Green, CRGB::Blue }, 3 }, 40, 100, DIR_FORWARD, 1, 3, 1, 50, 0,  1, 1, 1, 0,    0, 0, 0 }, // XL Butterfly
  { { { CRGB::Blue, CRGB::Purple, CRGB::Red }, 3 }, 40, 100, DIR_FORWARD, 1, 3, 1, 50, 0, 1, 0, 2, 0,   0, 0, 0 }, // XL Plasma
  { { { CRGB::Red, CRGB::Green, CRGB::Blue }, 3 }, 50, 100, DIR_FORWARD, 1, 3, 1, 50, 0,  2, 1, 1, 0,    0, 0, 1 }, // XL SingleStrand
  { { { CRGB::White, CRGB::Blue }, 2 }, 40, 100, DIR_FORWARD, 2, 3, 1, 50, 0,             1, 0, 1, 0,    0, 0, 0 }, // XL Morph
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
    case EFFECT_CHASE:            effectChase(p, nowMs); break;
    case EFFECT_SPIRAL:           effectSpiral(p, nowMs); break;
    case EFFECT_SNOW:             effectSnow(p, nowMs); break;
    case EFFECT_PINWHEEL:         effectPinwheel(p, nowMs); break;
    case EFFECT_COLORWASH:        effectColorwash(p, nowMs); break;
    case EFFECT_FIRE:             effectFire(p, nowMs); break;
    case EFFECT_CONFETTI:         effectConfetti(p, nowMs); break;
    case EFFECT_RIPPLE:           effectRipple(p, nowMs); break;
    case EFFECT_XL_BARS:          xlBars(p, nowMs); break;
    case EFFECT_XL_COLORWASH:     xlColorWash(p, nowMs); break;
    case EFFECT_XL_SPIRALS:       xlSpirals(p, nowMs); break;
    case EFFECT_XL_PINWHEEL:      xlPinwheel(p, nowMs); break;
    case EFFECT_XL_BUTTERFLY:     xlButterfly(p, nowMs); break;
    case EFFECT_XL_PLASMA:        xlPlasma(p, nowMs); break;
    case EFFECT_XL_SINGLESTRAND:  xlSingleStrand(p, nowMs); break;
    case EFFECT_XL_MORPH:         xlMorph(p, nowMs); break;
    case EFFECT_OFF:
    default:
      return; // already blacked out by setActiveEffect
  }
  scaleArrayBrightness((uint16_t)255 * p.intensity / 100);
  showAll();
}
