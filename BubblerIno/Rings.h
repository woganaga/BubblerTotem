#pragma once
#include <FastLED.h>

#define DATA_PIN_A   4  // drives rings 3, 2, 1 (in that chain order)
#define DATA_PIN_B   5  // drives rings 4, 5, 6 (in that chain order)

#define LEDS_PER_RING    27
#define RINGS_PER_PIN    3
#define LEDS_PER_PIN     (LEDS_PER_RING * RINGS_PER_PIN)
#define NUM_RINGS        6
#define NUM_LEDS_TOTAL   (LEDS_PER_RING * NUM_RINGS)

#define MAX_LED_BRIGHTNESS_PCT     40  // cap on any single LED, enforced on write
#define MAX_ARRAY_BRIGHTNESS_PCT   15  // cap on total array output, enforced on show

extern CRGB ledsA[LEDS_PER_PIN];
extern CRGB ledsB[LEDS_PER_PIN];

void ringsInit();

// ringNum is 1-6; pos 0 is the LED at 12:00, wrapping around the ring
CRGB& ringLED(uint8_t ringNum, uint8_t pos);
void setRingLED(uint8_t ringNum, uint8_t pos, CRGB color);
void setRing(uint8_t ringNum, CRGB color);
void setAll(CRGB color);
void showAll();

// normalized vertical height of a ring position: 0 = top (12:00), 1 = bottom
float ringPosHeight(uint8_t pos);
