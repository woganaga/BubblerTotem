#pragma once
#include "EffectManager.h"

// Saved, named effect configurations ("presets"): an effect type + its
// parameters + which saved palette (if any) it uses + which category (if
// any) it belongs to. Shown on the Saved Effects page. Categories will
// later be used to randomly pick an effect from within a category for an
// automated show. Persisted to LittleFS as a fixed-size flat array, so ids
// are stable slot indices that survive deletes elsewhere.

#define MAX_EFFECT_PRESETS 40
#define PRESET_NAME_LEN    24
#define PRESET_ID_NONE     255

struct EffectPreset {
  bool used;
  char name[PRESET_NAME_LEN];
  EffectId effectType;
  uint8_t paletteId;   // PALETTE_ID_NONE if not linked to a saved palette
  uint8_t categoryId;  // CATEGORY_ID_NONE if uncategorized
  EffectParams params; // params.palette is a snapshot, used as a fallback if
                        // paletteId's palette is later deleted (or if never linked)
};

void presetStoreInit();

uint8_t presetListIds(uint8_t* outIds, uint8_t maxIds);
const EffectPreset* presetGet(uint8_t id);

// returns the new id, or PRESET_ID_NONE if the store is full
uint8_t presetCreate(const char* name, EffectId type, uint8_t paletteId, uint8_t categoryId, const EffectParams& params);
bool presetUpdate(uint8_t id, const char* name, EffectId type, uint8_t paletteId, uint8_t categoryId, const EffectParams& params);
bool presetDelete(uint8_t id);
