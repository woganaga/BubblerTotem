#include "Rings.h"
#include <Arduino.h>

CRGB ledsA[LEDS_PER_PIN];
CRGB ledsB[LEDS_PER_PIN];

struct RingInfo {
  CRGB* strip;
  uint16_t offset;
};

static RingInfo ringInfo[NUM_RINGS] = {
  { ledsA, LEDS_PER_RING * 2 }, // ring 1
  { ledsA, LEDS_PER_RING * 1 }, // ring 2
  { ledsA, LEDS_PER_RING * 0 }, // ring 3
  { ledsB, LEDS_PER_RING * 0 }, // ring 4
  { ledsB, LEDS_PER_RING * 1 }, // ring 5
  { ledsB, LEDS_PER_RING * 2 }, // ring 6
};

static float ringPosHeightTable[LEDS_PER_RING];

void ringsInit() {
  FastLED.addLeds<WS2811, DATA_PIN_A, GRB>(ledsA, LEDS_PER_PIN);
  FastLED.addLeds<WS2811, DATA_PIN_B, GRB>(ledsB, LEDS_PER_PIN);

  // symmetric about the vertical axis, so LED wiring direction (CW/CCW) doesn't matter
  for (uint8_t pos = 0; pos < LEDS_PER_RING; pos++) {
    float angle = 2.0f * PI * pos / LEDS_PER_RING;
    ringPosHeightTable[pos] = (1.0f - cosf(angle)) / 2.0f;
  }

  setAll(CRGB::Black);
  showAll();
}

CRGB& ringLED(uint8_t ringNum, uint8_t pos) {
  RingInfo& r = ringInfo[ringNum - 1];
  return r.strip[r.offset + (pos % LEDS_PER_RING)];
}

static CRGB clampLEDBrightness(CRGB color) {
  uint8_t maxChannel = max(color.r, max(color.g, color.b));
  uint8_t capValue = (uint16_t)255 * MAX_LED_BRIGHTNESS_PCT / 100;
  if (maxChannel > capValue) {
    color.nscale8((uint16_t)capValue * 255 / maxChannel);
  }
  return color;
}

void setRingLED(uint8_t ringNum, uint8_t pos, CRGB color) {
  ringLED(ringNum, pos) = clampLEDBrightness(color);
}

void setRing(uint8_t ringNum, CRGB color) {
  for (uint8_t pos = 0; pos < LEDS_PER_RING; pos++) {
    setRingLED(ringNum, pos, color);
  }
}

void setAll(CRGB color) {
  for (uint8_t ring = 1; ring <= NUM_RINGS; ring++) {
    setRing(ring, color);
  }
}

void showAll() {
  uint32_t total = 0;
  for (uint16_t i = 0; i < LEDS_PER_PIN; i++) {
    total += max(ledsA[i].r, max(ledsA[i].g, ledsA[i].b));
    total += max(ledsB[i].r, max(ledsB[i].g, ledsB[i].b));
  }

  uint32_t budget = (uint32_t)NUM_LEDS_TOTAL * 255 * MAX_ARRAY_BRIGHTNESS_PCT / 100;

  if (total > budget) {
    uint8_t scale = (uint8_t)((uint64_t)budget * 255 / total);
    for (uint16_t i = 0; i < LEDS_PER_PIN; i++) {
      ledsA[i].nscale8(scale);
      ledsB[i].nscale8(scale);
    }
  }

  FastLED.show();
}

float ringPosHeight(uint8_t pos) {
  return ringPosHeightTable[pos % LEDS_PER_RING];
}
