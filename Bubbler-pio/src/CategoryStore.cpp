#include "CategoryStore.h"
#include "LittleFS.h"
#include <string.h>

static const char* CATEGORIES_FILE = "/categories.bin";
static Category categories[MAX_CATEGORIES];

static void save() {
  File f = LittleFS.open(CATEGORIES_FILE, "w");
  if (!f) return;
  f.write((const uint8_t*)categories, sizeof(categories));
  f.close();
}

void categoryStoreInit() {
  LittleFS.begin(true);
  memset(categories, 0, sizeof(categories));

  File f = LittleFS.open(CATEGORIES_FILE, "r");
  if (f) {
    if (f.size() == sizeof(categories)) f.read((uint8_t*)categories, sizeof(categories));
    f.close();
  }
}

uint8_t categoryListIds(uint8_t* outIds, uint8_t maxIds) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < MAX_CATEGORIES && n < maxIds; i++) {
    if (categories[i].used) outIds[n++] = i;
  }
  return n;
}

const Category* categoryGet(uint8_t id) {
  if (id >= MAX_CATEGORIES || !categories[id].used) return nullptr;
  return &categories[id];
}

uint8_t categoryFindByName(const char* name) {
  for (uint8_t i = 0; i < MAX_CATEGORIES; i++) {
    if (categories[i].used && strcmp(categories[i].name, name) == 0) return i;
  }
  return CATEGORY_ID_NONE;
}

uint8_t categoryGetOrCreate(const char* name) {
  if (!name || name[0] == '\0') return CATEGORY_ID_NONE;

  uint8_t existing = categoryFindByName(name);
  if (existing != CATEGORY_ID_NONE) return existing;

  for (uint8_t i = 0; i < MAX_CATEGORIES; i++) {
    if (!categories[i].used) {
      categories[i].used = true;
      strncpy(categories[i].name, name, CATEGORY_NAME_LEN - 1);
      categories[i].name[CATEGORY_NAME_LEN - 1] = '\0';
      save();
      return i;
    }
  }
  return CATEGORY_ID_NONE;
}

bool categoryDelete(uint8_t id) {
  if (id >= MAX_CATEGORIES || !categories[id].used) return false;
  categories[id].used = false;
  save();
  return true;
}
