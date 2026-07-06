#pragma once
#include <Arduino.h>
#include "Palette.h"

// Sweeps a bar per palette color (stacked consecutively) from the top of every
// ring to the bottom, looping every periodMs. barHeightFraction sets each bar's
// height as a fraction of the ring's vertical span.
void effectVerticalSweep(const Palette& palette, uint32_t periodMs, uint32_t nowMs,
                          float barHeightFraction = 1.0f / 9.0f);
