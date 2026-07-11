#pragma once
#include <Arduino.h>

#define RING_DIAMETER_MM     145.0f
#define RING_SPACING_MM      50.0f   // center-to-center spacing between adjacent rings
#define RING_GAP_3_4_MM      100.0f  // wider center-to-center spacing between ring 3 and ring 4

// center position of each ring along the totem's axis, in mm; ring 1 is 0
extern const float RING_POSITION_MM[];

float ringPositionMM(uint8_t ringNum);  // ringNum is 1-6
float ringCircumferenceMM();
float ledArcPitchMM();                  // physical spacing between adjacent LEDs around a ring
float totemLengthMM();                  // distance from ring 1's center to ring 6's center
