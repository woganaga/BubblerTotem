#pragma once
#include <FastLED.h>
#include <math.h>
#include "Palette.h"

// Shared color/palette math used by the effect modules. Kept header-only and
// inline since these are tiny and called per-pixel.

// Blend two colors by a 0..1 fraction.
static inline CRGB blendPct(CRGB a, CRGB b, float pct) {
  if (pct < 0) pct = 0;
  if (pct > 1) pct = 1;
  return blend(a, b, (uint8_t)(pct * 255));
}

// Scale a color's brightness by a 0..1 factor.
static inline CRGB scaleColor(CRGB c, float f) {
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  c.nscale8((uint8_t)(f * 255));
  return c;
}

// Rainbow hue -> RGB (pure hue ramp, saturation/value = 1), h in 0..1.
static inline CRGB rainbowHue(float h) {
  if (h < 0) h = 0;
  if (h > 1) h = 1;
  float hue = h * 6.0f;
  int i = (int)floorf(hue);
  float f = hue - i;
  uint8_t up = (uint8_t)(f * 255), down = (uint8_t)((1.0f - f) * 255);
  switch (i) {
    case 0:  return CRGB(255, up, 0);
    case 1:  return CRGB(down, 255, 0);
    case 2:  return CRGB(0, 255, up);
    case 3:  return CRGB(0, down, 255);
    case 4:  return CRGB(up, 0, 255);
    default: return CRGB(255, 0, down);
  }
}

// Blend across the whole palette; n in 0..1. circular wraps back to color[0]
// (so n=1 meets n=0), otherwise n spans color[0]..color[count-1].
static inline CRGB paletteBlend(const Palette& pal, float n, bool circular) {
  if (pal.count <= 1) return pal.colors[0];
  if (n >= 1.0f) n = 0.999999f;
  if (n < 0.0f) n = 0.0f;
  float nc = pal.count;
  float realidx = circular ? n * nc : n * (nc - 1.0f);
  int i1 = (int)floorf(realidx);
  int i2 = (i1 + 1) % pal.count;
  uint8_t ratio = (uint8_t)((realidx - i1) * 255);
  return blend(pal.colors[i1], pal.colors[i2], ratio);
}
