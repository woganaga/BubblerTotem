#pragma once
#include <Arduino.h>
#include "EffectParams.h"

// Sweeps a train of bars (one per palette color) from the top of every ring
// to the bottom (or reverse/bounce per params.direction). Colors crossfade
// and the train fades in/out of black at its edges. params.width sets each
// bar's height as a multiple of one LED's worth of ring resolution.
void effectVerticalSweep(const EffectParams& params, uint32_t nowMs);

// Same as effectVerticalSweep, but sweeps whole rings left/right along the
// totem's axis (using real inter-ring spacing from Topology.h) instead of
// individual LEDs within a ring.
void effectHorizontalSweep(const EffectParams& params, uint32_t nowMs);

// Rings 1-3 and rings 4-6 alternate on/off out of phase. Uses palette
// colors 0 and 1 (color 0 for both sides if the palette only has one).
void effectAlternateFlash(const EffectParams& params, uint32_t nowMs);

// A helical band of color twists along the totem length. Direction can also
// be DIR_BOUNCE_IN (ends converge on the center) or DIR_BOUNCE_OUT (center
// expands to the ends).
void effectSpiral(const EffectParams& params, uint32_t nowMs);

// Single-pixel "flakes" spawn at random intervals at the top of a random
// ring and step down one side of the ring until they reach the bottom.
void effectSnow(const EffectParams& params, uint32_t nowMs);
