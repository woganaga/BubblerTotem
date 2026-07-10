#include "EffectManager.h"
#include "Effects.h"
#include "XLFX.h"
#include "Rings.h"
#include "PaletteStore.h"
#include "EffectPresetStore.h"
#include "AudioInput.h"

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
static uint8_t effectPaletteId[EFFECT_COUNT];
static uint8_t activePresetId = PRESET_ID_NONE;

static bool beatSyncEnabled = false;
static uint32_t beatSyncAnchorMs = 0; // nowMs at the most recent detected beat onset
static bool beatSyncWasActive = false; // for edge-detecting audioBeatActive()'s ~120ms-long pulse

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

void effectManagerInit() {
  for (uint8_t i = 0; i < EFFECT_COUNT; i++) effectPaletteId[i] = PALETTE_ID_NONE;
  activePresetId = PRESET_ID_NONE;
}

void setActiveEffect(EffectId id) {
  activeEffect = id;
  activePresetId = PRESET_ID_NONE;
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

uint8_t getEffectPaletteId(EffectId id) { return effectPaletteId[id]; }
void setEffectPaletteId(EffectId id, uint8_t paletteId) { effectPaletteId[id] = paletteId; }

uint8_t getActivePresetId() { return activePresetId; }
void setActivePresetId(uint8_t presetId) { activePresetId = presetId; }

void setBeatSyncEnabled(bool enabled) { beatSyncEnabled = enabled; }
bool getBeatSyncEnabled() { return beatSyncEnabled; }

bool loadEffectPreset(uint8_t presetId) {
  const EffectPreset* preset = presetGet(presetId);
  if (!preset) return false;

  activeEffect = preset->effectType;
  effectParams[preset->effectType] = preset->params;
  effectPaletteId[preset->effectType] = preset->paletteId;
  activePresetId = presetId;

  if (activeEffect == EFFECT_OFF) {
    setAll(CRGB::Black);
    showAll();
  }
  return true;
}

void runActiveEffect(uint32_t nowMs) {
  uint8_t linkedPalette = effectPaletteId[activeEffect];
  if (linkedPalette != PALETTE_ID_NONE) {
    const NamedPalette* np = paletteGet(linkedPalette);
    if (np) {
      effectParams[activeEffect].palette = np->palette;
    } else {
      effectPaletteId[activeEffect] = PALETTE_ID_NONE; // saved palette was deleted
    }
  }

  uint32_t effectNowMs = nowMs;
  if (beatSyncEnabled) {
    // audioBeatActive() stays true for a ~120ms flash after each onset, so
    // only re-anchor on its rising edge - otherwise every one of the many
    // loop() calls during that window would reset the anchor to "now",
    // instead of to the moment the beat actually started.
    bool beatNow = audioBeatActive();
    if (beatNow && !beatSyncWasActive) beatSyncAnchorMs = nowMs;
    beatSyncWasActive = beatNow;
    effectNowMs = nowMs - beatSyncAnchorMs; // elapsed time since the most recent beat onset
  }

  const EffectParams& p = effectParams[activeEffect];
  switch (activeEffect) {
    case EFFECT_VERTICAL_SWEEP:   effectVerticalSweep(p, effectNowMs); break;
    case EFFECT_HORIZONTAL_SWEEP: effectHorizontalSweep(p, effectNowMs); break;
    case EFFECT_ALTERNATE_FLASH:  effectAlternateFlash(p, effectNowMs); break;
    case EFFECT_CHASE:            effectChase(p, effectNowMs); break;
    case EFFECT_SPIRAL:           effectSpiral(p, effectNowMs); break;
    case EFFECT_SNOW:             effectSnow(p, effectNowMs); break;
    case EFFECT_PINWHEEL:         effectPinwheel(p, effectNowMs); break;
    case EFFECT_COLORWASH:        effectColorwash(p, effectNowMs); break;
    case EFFECT_FIRE:             effectFire(p, effectNowMs); break;
    case EFFECT_CONFETTI:         effectConfetti(p, effectNowMs); break;
    case EFFECT_RIPPLE:           effectRipple(p, effectNowMs); break;
    case EFFECT_XL_BARS:          xlBars(p, effectNowMs); break;
    case EFFECT_XL_COLORWASH:     xlColorWash(p, effectNowMs); break;
    case EFFECT_XL_SPIRALS:       xlSpirals(p, effectNowMs); break;
    case EFFECT_XL_PINWHEEL:      xlPinwheel(p, effectNowMs); break;
    case EFFECT_XL_BUTTERFLY:     xlButterfly(p, effectNowMs); break;
    case EFFECT_XL_PLASMA:        xlPlasma(p, effectNowMs); break;
    case EFFECT_XL_SINGLESTRAND:  xlSingleStrand(p, effectNowMs); break;
    case EFFECT_XL_MORPH:         xlMorph(p, effectNowMs); break;
    case EFFECT_OFF:
    default:
      return; // already blacked out by setActiveEffect
  }
  scaleArrayBrightness((uint16_t)255 * p.intensity / 100);
  showAll();
}
