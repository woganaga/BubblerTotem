#include "Effects.h"
#include "Rings.h"

void effectVerticalSweep(const Palette& palette, uint32_t periodMs, uint32_t nowMs,
                          float barHeightFraction) {
  const float trainHeight = barHeightFraction * palette.count;

  // 0..1 over periodMs; leadingEdge runs from fully-above-top to fully-below-bottom
  float phase = (float)(nowMs % periodMs) / (float)periodMs;
  float leadingEdge = phase * (1.0f + trainHeight) - trainHeight;

  for (uint8_t ring = 1; ring <= NUM_RINGS; ring++) {
    for (uint8_t pos = 0; pos < LEDS_PER_RING; pos++) {
      float distanceIntoTrain = ringPosHeight(pos) - leadingEdge;

      if (distanceIntoTrain >= 0.0f && distanceIntoTrain < trainHeight) {
        uint8_t colorIndex = (uint8_t)(distanceIntoTrain / barHeightFraction);
        if (colorIndex >= palette.count) colorIndex = palette.count - 1;
        setRingLED(ring, pos, palette.colors[colorIndex]);
      } else {
        setRingLED(ring, pos, CRGB::Black);
      }
    }
  }
}
