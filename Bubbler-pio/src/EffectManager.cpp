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
static float beatSyncBeatsOverride = 0.0f; // beats per effect cycle; 0 = auto
static float beatSyncAutoBeats = 1.0f;     // auto mode's current pick (kept across frames for hysteresis)
static bool beatSyncLocked = false;        // tempo lock engaged (confidence hysteresis below)
static float beatSyncActiveM = 0.0f;       // multiple in use this frame; 0 = free-running

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

void setBeatSyncBeats(float beats) { beatSyncBeatsOverride = beats; }
float getBeatSyncBeats() { return beatSyncBeatsOverride; }
bool beatSyncLockedNow() { return beatSyncEnabled && beatSyncLocked; }
float beatSyncActiveBeats() { return beatSyncEnabled ? beatSyncActiveM : 0.0f; }

// full visual cycle length of the active effect at its current params, or 0
// if it has no fixed loop (see Effects.h / XLFX.h)
static float activeEffectCycleMs() {
  if (activeEffect >= EFFECT_XL_BARS) return xlfxNativePeriodMs(activeEffect, effectParams[activeEffect]);
  return effectNativePeriodMs(activeEffect, effectParams[activeEffect]);
}

// Synthesizes the active effect's time input from the audio PLL's beat clock
// so one effect cycle spans exactly M beats, phase-aligned to the beat: the
// returned time is (musical position in beats / M, wrapped to one cycle) *
// cycleMs, so it sweeps 0..cycleMs once per M beats and snaps back to 0 ON a
// beat. Falls back to real time while the tempo estimator isn't confidently
// locked (with hysteresis so a borderline confidence doesn't flap the mode),
// or when the effect has no fixed cycle to fit to the tempo.
static uint32_t beatSyncedTimeMs(uint32_t nowMs, float cycleMs) {
  AudioFeatures f = audioFeatures();

  if (!beatSyncLocked) {
    if (f.bpm > 1.0f && f.confidence >= 0.30f) beatSyncLocked = true;
  } else if (f.bpm <= 1.0f || f.confidence < 0.12f) {
    beatSyncLocked = false;
  }
  if (!beatSyncLocked || cycleMs <= 0.0f) {
    beatSyncActiveM = 0.0f;
    return nowMs;
  }

  float M = beatSyncBeatsOverride;
  if (M <= 0.0f) {
    // Auto: pick the multiple whose resulting cycle is log-closest to the
    // cycle the user's speed setting implies. Only switch away from the
    // current pick when the winner is clearly better (0.15 in log2 terms),
    // so BPM jitter near a boundary doesn't flip the visible rate around.
    static const float CHOICES[] = { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };
    float idealBeats = cycleMs / (60000.0f / f.bpm);
    float best = beatSyncAutoBeats;
    float bestErr = 1e9f;
    for (uint8_t i = 0; i < sizeof(CHOICES) / sizeof(CHOICES[0]); i++) {
      float err = fabsf(log2f(idealBeats / CHOICES[i]));
      if (err < bestErr) { bestErr = err; best = CHOICES[i]; }
    }
    float currentErr = fabsf(log2f(idealBeats / beatSyncAutoBeats));
    if (best != beatSyncAutoBeats && bestErr + 0.15f < currentErr) beatSyncAutoBeats = best;
    M = beatSyncAutoBeats;
  }
  beatSyncActiveM = M;

  // Continuous musical position in beats. The DSP publishes ~32 snapshots/s,
  // so extrapolate beatPhase forward from the snapshot's timestamp at the
  // current tempo to avoid ~31ms time-quantization stutter in the animation.
  // beatCount is wrapped (mod a multiple of every allowed M) before going
  // float so precision doesn't degrade with uptime; the wrap lands exactly
  // on a cycle boundary so it's invisible.
  float beats = (float)(f.beatCount & 511u) + f.beatPhase
              + (float)(int32_t)(nowMs - f.frameMs) * (f.bpm / 60000.0f);
  float cyclePos = beats / M;
  cyclePos -= floorf(cyclePos); // 0..1 within the current M-beat cycle
  return (uint32_t)(cyclePos * cycleMs);
}

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
  if (beatSyncEnabled && activeEffect != EFFECT_OFF) {
    effectNowMs = beatSyncedTimeMs(nowMs, activeEffectCycleMs());
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
