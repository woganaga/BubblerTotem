#pragma once
#include "Palette.h"

enum Direction {
  DIR_FORWARD = 0,  // effect's base direction (down for vertical, right for horizontal/spiral)
  DIR_REVERSE,      // mirror of the base direction (up / left)
  DIR_BOUNCE,       // ping-pongs between forward and reverse
  DIR_BOUNCE_IN,    // spiral-only: ends move inward to the center, then back out
  DIR_BOUNCE_OUT,   // spiral-only: center moves outward to the ends, then back in
};

struct EffectParams {
  Palette palette;
  uint8_t speedPct;   // 1-100, higher is faster
  Direction direction;
  uint8_t width;      // 1-3; meaning depends on the effect, ignored where not applicable
};
