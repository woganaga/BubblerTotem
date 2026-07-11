#pragma once
#include <Arduino.h>

// Named categories for saved effects (see EffectPresetStore.h). Not used by
// the firmware yet beyond storage/display - the plan is to later pick a
// random effect from within a category for an automated show. Persisted to
// LittleFS as a fixed-size flat array, so ids are stable slot indices.

#define MAX_CATEGORIES    16
#define CATEGORY_NAME_LEN 24
#define CATEGORY_ID_NONE  255

struct Category {
  bool used;
  char name[CATEGORY_NAME_LEN];
};

void categoryStoreInit();

uint8_t categoryListIds(uint8_t* outIds, uint8_t maxIds);
const Category* categoryGet(uint8_t id);
uint8_t categoryFindByName(const char* name);

// finds an existing category by exact name, or creates one if none match;
// returns CATEGORY_ID_NONE if name is empty or the store is full
uint8_t categoryGetOrCreate(const char* name);

bool categoryDelete(uint8_t id);
