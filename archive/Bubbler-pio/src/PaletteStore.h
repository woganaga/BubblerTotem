#pragma once
#include "Palette.h"

// Named, saved color palettes that effects can reference (see
// EffectManager's getEffectPaletteId/setEffectPaletteId and
// EffectPresetStore.h). Persisted to LittleFS as a fixed-size flat array,
// so ids are stable slot indices that survive deletes elsewhere.

#define MAX_PALETTES     24
#define PALETTE_NAME_LEN 24
#define PALETTE_ID_NONE  255

struct NamedPalette {
  bool used;
  char name[PALETTE_NAME_LEN];
  Palette palette;
};

// loads from flash, seeding a handful of starter palettes on first boot
void paletteStoreInit();

// writes up to maxIds valid ids (in slot order) into outIds; returns the count
uint8_t paletteListIds(uint8_t* outIds, uint8_t maxIds);

// nullptr if id is out of range or not in use
const NamedPalette* paletteGet(uint8_t id);

// returns the new id, or PALETTE_ID_NONE if the store is full
uint8_t paletteCreate(const char* name, const Palette& pal);
bool paletteUpdate(uint8_t id, const char* name, const Palette& pal);
bool paletteDelete(uint8_t id);
