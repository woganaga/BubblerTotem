#include "PaletteStore.h"
#include "LittleFS.h"
#include <string.h>

static const char* PALETTES_FILE = "/palettes.bin";
static NamedPalette palettes[MAX_PALETTES];

static void save() {
  File f = LittleFS.open(PALETTES_FILE, "w");
  if (!f) return;
  f.write((const uint8_t*)palettes, sizeof(palettes));
  f.close();
}

static void setName(char* dst, const char* src) {
  strncpy(dst, src, PALETTE_NAME_LEN - 1);
  dst[PALETTE_NAME_LEN - 1] = '\0';
}

// A handful of starter palettes so the picker isn't empty on first boot.
static void seedDefaults() {
  struct Seed { const char* name; uint8_t count; CRGB colors[MAX_PALETTE_COLORS]; };
  static const Seed seeds[] = {
    { "RGB",         3, { CRGB::Red, CRGB::Green, CRGB::Blue } },
    { "Fire",        3, { CRGB::Red, CRGB::Orange, CRGB::Yellow } },
    { "Ocean",       2, { CRGB::Blue, CRGB::Cyan } },
    { "Purple Rain", 3, { CRGB::Red, CRGB::Purple, CRGB::Blue } },
    { "Mono White",  1, { CRGB::White } },
  };
  for (uint8_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
    palettes[i].used = true;
    setName(palettes[i].name, seeds[i].name);
    palettes[i].palette.count = seeds[i].count;
    for (uint8_t c = 0; c < MAX_PALETTE_COLORS; c++) {
      palettes[i].palette.colors[c] = (c < seeds[i].count) ? seeds[i].colors[c] : CRGB::Black;
    }
  }
  save();
}

void paletteStoreInit() {
  LittleFS.begin(true);
  memset(palettes, 0, sizeof(palettes));

  File f = LittleFS.open(PALETTES_FILE, "r");
  bool loaded = false;
  if (f) {
    if (f.size() == sizeof(palettes)) {
      f.read((uint8_t*)palettes, sizeof(palettes));
      loaded = true;
    }
    f.close();
  }
  if (!loaded) seedDefaults();
}

uint8_t paletteListIds(uint8_t* outIds, uint8_t maxIds) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < MAX_PALETTES && n < maxIds; i++) {
    if (palettes[i].used) outIds[n++] = i;
  }
  return n;
}

const NamedPalette* paletteGet(uint8_t id) {
  if (id >= MAX_PALETTES || !palettes[id].used) return nullptr;
  return &palettes[id];
}

static int findFreeSlot() {
  for (uint8_t i = 0; i < MAX_PALETTES; i++) if (!palettes[i].used) return i;
  return -1;
}

uint8_t paletteCreate(const char* name, const Palette& pal) {
  int slot = findFreeSlot();
  if (slot < 0) return PALETTE_ID_NONE;
  palettes[slot].used = true;
  setName(palettes[slot].name, name);
  palettes[slot].palette = pal;
  save();
  return (uint8_t)slot;
}

bool paletteUpdate(uint8_t id, const char* name, const Palette& pal) {
  if (id >= MAX_PALETTES || !palettes[id].used) return false;
  setName(palettes[id].name, name);
  palettes[id].palette = pal;
  save();
  return true;
}

bool paletteDelete(uint8_t id) {
  if (id >= MAX_PALETTES || !palettes[id].used) return false;
  palettes[id].used = false;
  save();
  return true;
}
