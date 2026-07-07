#pragma once
#include "Palette.h"

enum Direction {
  DIR_FORWARD = 0,  // effect's base direction (down for vertical, right for horizontal/chase)
  DIR_REVERSE,      // mirror of the base direction (up / left)
  DIR_BOUNCE,       // ping-pongs between forward and reverse
  DIR_BOUNCE_IN,    // spiral-only: ends move inward to the center, then back out
  DIR_BOUNCE_OUT,   // spiral-only: center moves outward to the ends, then back in
};

struct EffectParams {
  Palette palette;
  uint8_t speedPct;   // 1-100, higher is faster
  uint8_t intensity;  // 1-100, overall brightness of the effect's output; 100 = full
  Direction direction;
  uint8_t width;      // 1-3; meaning depends on the effect, ignored where not applicable
  uint8_t twists;     // 1-10; spiral-only: number of full rotations across the totem length
  uint8_t rows;       // 1-6; spiral-only: parallel strands evenly spaced around the ring
  uint8_t overlap;    // 1-100; alternate-flash-only: how much both sides overlap while crossfading
  uint8_t dualChase;  // 0 or 1; chase-only: mirrors a second chase starting from the opposite end
};
