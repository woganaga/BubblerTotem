#include "Topology.h"
#include "Rings.h"

const float RING_POSITION_MM[NUM_RINGS] = {
  0.0f,                                                        // ring 1
  RING_SPACING_MM,                                              // ring 2
  RING_SPACING_MM * 2,                                          // ring 3
  RING_SPACING_MM * 2 + RING_GAP_3_4_MM,                        // ring 4
  RING_SPACING_MM * 2 + RING_GAP_3_4_MM + RING_SPACING_MM,      // ring 5
  RING_SPACING_MM * 2 + RING_GAP_3_4_MM + RING_SPACING_MM * 2,  // ring 6
};

float ringPositionMM(uint8_t ringNum) {
  return RING_POSITION_MM[ringNum - 1];
}

float ringCircumferenceMM() {
  return PI * RING_DIAMETER_MM;
}

float ledArcPitchMM() {
  return ringCircumferenceMM() / LEDS_PER_RING;
}

float totemLengthMM() {
  return RING_POSITION_MM[NUM_RINGS - 1] - RING_POSITION_MM[0];
}
