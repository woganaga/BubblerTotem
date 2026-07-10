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

// When on, runActiveEffect() re-anchors every effect's timeline to 0 at each
// detected beat onset (instead of feeding it raw elapsed time), so whatever
// effect is running restarts in sync with the beat and repeats at the
// detected tempo - no changes needed to individual effects, since they
// already treat their nowMs argument as "time since some start point".
void setBeatSyncEnabled(bool enabled);
bool getBeatSyncEnabled();
