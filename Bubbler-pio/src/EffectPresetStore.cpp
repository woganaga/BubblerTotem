#include "EffectPresetStore.h"
#include "LittleFS.h"
#include <string.h>

static const char* PRESETS_FILE = "/effects.bin";
static EffectPreset presets[MAX_EFFECT_PRESETS];

static void save() {
  File f = LittleFS.open(PRESETS_FILE, "w");
  if (!f) return;
  f.write((const uint8_t*)presets, sizeof(presets));
  f.close();
}

static void fill(EffectPreset& slot, const char* name, EffectId type, uint8_t paletteId, uint8_t categoryId, const EffectParams& params) {
  slot.used = true;
  strncpy(slot.name, name, PRESET_NAME_LEN - 1);
  slot.name[PRESET_NAME_LEN - 1] = '\0';
  slot.effectType = type;
  slot.paletteId = paletteId;
  slot.categoryId = categoryId;
  slot.params = params;
}

void presetStoreInit() {
  LittleFS.begin(true);
  memset(presets, 0, sizeof(presets));

  File f = LittleFS.open(PRESETS_FILE, "r");
  if (f) {
    if (f.size() == sizeof(presets)) f.read((uint8_t*)presets, sizeof(presets));
    f.close();
  }
}

uint8_t presetListIds(uint8_t* outIds, uint8_t maxIds) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < MAX_EFFECT_PRESETS && n < maxIds; i++) {
    if (presets[i].used) outIds[n++] = i;
  }
  return n;
}

const EffectPreset* presetGet(uint8_t id) {
  if (id >= MAX_EFFECT_PRESETS || !presets[id].used) return nullptr;
  return &presets[id];
}

uint8_t presetCreate(const char* name, EffectId type, uint8_t paletteId, uint8_t categoryId, const EffectParams& params) {
  for (uint8_t i = 0; i < MAX_EFFECT_PRESETS; i++) {
    if (!presets[i].used) {
      fill(presets[i], name, type, paletteId, categoryId, params);
      save();
      return i;
    }
  }
  return PRESET_ID_NONE;
}

bool presetUpdate(uint8_t id, const char* name, EffectId type, uint8_t paletteId, uint8_t categoryId, const EffectParams& params) {
  if (id >= MAX_EFFECT_PRESETS || !presets[id].used) return false;
  fill(presets[id], name, type, paletteId, categoryId, params);
  save();
  return true;
}

bool presetDelete(uint8_t id) {
  if (id >= MAX_EFFECT_PRESETS || !presets[id].used) return false;
  presets[id].used = false;
  save();
  return true;
}
