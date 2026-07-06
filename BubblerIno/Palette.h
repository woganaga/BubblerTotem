#pragma once
#include <FastLED.h>

#define MAX_PALETTE_COLORS 10

struct Palette {
  CRGB colors[MAX_PALETTE_COLORS];
  uint8_t count; // 1-10
};
