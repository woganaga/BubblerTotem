#pragma once
#include <Arduino.h>
#include "EffectParams.h"
#include "EffectManager.h" // EffectId, for xlfxNativePeriodMs

// Full visual repeat length in ms for one of the xLights-derived effects at
// the given params, or 0 for the field effects (Butterfly, Plasma) whose
// animation never exactly repeats. See Effects.h's effectNativePeriodMs.
float xlfxNativePeriodMs(EffectId id, const EffectParams& params);

// xLights-derived effects. Each renders into a 27x6 scratch buffer (the
// unrolled cylinder: x = position around a ring, y = ring index) and commits
// 1:1 to the physical LEDs. These are additions - the original effects are
// untouched. Time is taken from nowMs; params.speedPct scales animation rate.
//
// Shared param mapping (see EffectParams.h): count, style, density, thickness,
// twistDeg, flag, flag2, plus the common palette/speed/width/direction.

void xlBars(const EffectParams& params, uint32_t nowMs);        // scrolling color bands
void xlColorWash(const EffectParams& params, uint32_t nowMs);   // whole surface cycles the palette
void xlSpirals(const EffectParams& params, uint32_t nowMs);     // barber-pole diagonal strands
void xlPinwheel(const EffectParams& params, uint32_t nowMs);    // rotating arms (angular, twists into a helix)
void xlButterfly(const EffectParams& params, uint32_t nowMs);   // per-pixel butterfly field
void xlPlasma(const EffectParams& params, uint32_t nowMs);      // per-pixel plasma field
void xlSingleStrand(const EffectParams& params, uint32_t nowMs); // configurable chase with fade tails
void xlMorph(const EffectParams& params, uint32_t nowMs);       // a bright line sweeps across with a fading trail
