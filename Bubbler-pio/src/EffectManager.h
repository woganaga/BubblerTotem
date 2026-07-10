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
  // NOTE: only append new effects here - saved presets store effectType as
  // the raw numeric id (EffectPresetStore), so reordering breaks them.
  EFFECT_BEAT_FLASH, // whole-array flash pulse, for eyeballing beat/tempo sync
  EFFECT_COUNT
};

extern const char* const EFFECT_NAMES[EFFECT_COUNT];

// must be called once at boot (after the palette/category/preset stores'
// own *Init() calls), before anything else in this file is used
void effectManagerInit();

void setActiveEffect(EffectId id);
EffectId getActiveEffect();

// each effect remembers its own params across activations
EffectParams& effectParamsFor(EffectId id);

// call every loop() iteration; runs whichever effect is active and shows the frame
void runActiveEffect(uint32_t nowMs);

// Which saved palette (see PaletteStore.h) is linked to a given effect
// type's live params, or 255 if unlinked (the colors in effectParamsFor(id)
// are used as-is). While linked, runActiveEffect() re-resolves the palette
// into effectParamsFor(id).palette every frame, so editing a saved palette
// updates any effect currently using it live.
uint8_t getEffectPaletteId(EffectId id);
void setEffectPaletteId(EffectId id, uint8_t paletteId);

// Loads a saved effect preset (see EffectPresetStore.h) as the live, active
// configuration, including its palette link. Returns false if presetId
// doesn't resolve to a saved preset.
bool loadEffectPreset(uint8_t presetId);

// id of the saved preset the live config is currently tied to, or 255 if
// it isn't tied to one (e.g. after picking a raw effect type, or before
// ever saving one)
uint8_t getActivePresetId();
void setActivePresetId(uint8_t presetId);

// Beat sync: when on (and the tempo estimator is confidently locked),
// runActiveEffect() stops feeding the active effect real elapsed time and
// instead synthesizes its time input directly from the audio PLL's beat
// clock, scaled so one full effect cycle spans exactly N beats and cycle
// boundaries land on beats. The effect's animation then speeds up, slows
// down, and stays in phase with the music indefinitely - no changes needed
// to individual effects, since they're periodic pure functions of their
// time argument (see effectNativePeriodMs / xlfxNativePeriodMs). Stochastic
// effects with no fixed loop (Snow, Fire, Confetti, Ripple, XL Butterfly,
// XL Plasma) free-run even when sync is on. While unlocked (no confident
// tempo yet), everything free-runs.
void setBeatSyncEnabled(bool enabled);
bool getBeatSyncEnabled();

// Beats per effect cycle: 0 = auto (pick 1/2/4/8 or half a beat, whichever
// is log-closest to the cycle length the effect's speed slider implies),
// else an explicit 0.5 / 1 / 2 / 4 / 8. While locked, the speed slider no
// longer changes the visible rate directly - it just steers which multiple
// auto mode picks.
void setBeatSyncBeats(float beats);
float getBeatSyncBeats();

// live readouts for the UI (updated each runActiveEffect call)
bool beatSyncLockedNow();     // tempo lock currently engaged?
float beatSyncActiveBeats();  // beats per cycle actually in use right now; 0 = free-running
