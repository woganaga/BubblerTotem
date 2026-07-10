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

// Rings 1-3 and rings 4-6 crossfade back and forth out of phase. Uses
// palette colors 0 and 1 (color 0 for both sides if the palette only has
// one). params.overlap controls how much time both sides spend partially
// lit at once: low values snap quickly between sides, high values linger
// in a long blended crossfade.
void effectAlternateFlash(const EffectParams& params, uint32_t nowMs);

// A snake of pixels starts at one end, wraps fully around a ring, then
// advances to the next ring, and so on until it reaches the far end. If
// params.dualChase is set, a second snake mirrors the first, starting from
// the opposite end.
void effectChase(const EffectParams& params, uint32_t nowMs);

// A helical band of color twists along the totem length. params.twists sets
// how many full rotations it makes end-to-end (the "sine wave" size).
// Direction can also be DIR_BOUNCE_IN (ends converge on the center) or
// DIR_BOUNCE_OUT (center expands to the ends).
void effectSpiral(const EffectParams& params, uint32_t nowMs);

// Single-pixel "flakes" spawn at random intervals at the top of a random
// ring and step down one side of the ring until they reach the bottom.
void effectSnow(const EffectParams& params, uint32_t nowMs);

// Palette colors form wedges that continuously rotate around every ring
// (same rotation on all 6 rings, no axial motion). Direction is clockwise
// (DIR_FORWARD) or counterclockwise (DIR_REVERSE).
void effectPinwheel(const EffectParams& params, uint32_t nowMs);

// All LEDs fade in unison through every palette color in sequence, looping.
void effectColorwash(const EffectParams& params, uint32_t nowMs);

// Classic heat-simulation flicker (per ring, independent), colored by a
// black-to-palette gradient instead of the usual fixed fire colors.
void effectFire(const EffectParams& params, uint32_t nowMs);

// Random single-pixel sparkles anywhere in the array, in a random palette
// color, that fade out over time.
void effectConfetti(const EffectParams& params, uint32_t nowMs);

// Pulses spawn at the ring3/ring4 gap (the totem's physical center) and
// expand outward toward both ends, fading as they go, like a heartbeat.
void effectRipple(const EffectParams& params, uint32_t nowMs);
