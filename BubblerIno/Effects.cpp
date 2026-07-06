#include "Effects.h"
#include "Rings.h"
#include "Topology.h"

static uint32_t speedToPeriodMs(uint8_t speedPct, uint32_t minMs, uint32_t maxMs) {
  if (speedPct < 1) speedPct = 1;
  if (speedPct > 100) speedPct = 100;
  return maxMs - (uint32_t)((maxMs - minMs) * (uint32_t)(speedPct - 1) / 99);
}

// index 0 and (palette.count + 1) are virtual black bands, so a train fades
// in/out of black smoothly instead of switching on/off abruptly
static CRGB virtualPaletteColor(const Palette& palette, int index) {
  if (index <= 0 || index > palette.count) return CRGB::Black;
  return palette.colors[index - 1];
}

// Samples a color train (palette bands padded with black at both ends) at
// continuous coordinate `pos`, where the train's leading edge sits at
// `leadingEdge` and each band is `bandWidth` wide.
static CRGB trainColorAt(float pos, float leadingEdge, float bandWidth, const Palette& palette) {
  uint8_t virtualCount = palette.count + 2;
  float t = (pos - leadingEdge) / bandWidth;

  if (t < 0.0f || t >= virtualCount) return CRGB::Black;

  int index = (int)t;
  uint8_t frac = (uint8_t)((t - index) * 255);
  return blend(virtualPaletteColor(palette, index), virtualPaletteColor(palette, index + 1), frac);
}

struct SweepState {
  float leadingEdge;
  bool flip;
};

// axisLength is the normalized length of the travel axis (1.0 for a plain 0..1 axis)
static SweepState computeSweepState(Direction direction, uint32_t periodMs, uint32_t nowMs,
                                     float trainWidth, float axisLength) {
  SweepState s;
  switch (direction) {
    case DIR_REVERSE: {
      float phase = (float)(nowMs % periodMs) / (float)periodMs;
      s.leadingEdge = phase * (axisLength + trainWidth) - trainWidth;
      s.flip = true;
      break;
    }
    case DIR_BOUNCE: {
      uint32_t cycle = periodMs * 2;
      float t = (float)(nowMs % cycle) / (float)periodMs; // 0..2
      bool goingBack = t >= 1.0f;
      float legPhase = goingBack ? (t - 1.0f) : t;
      s.leadingEdge = legPhase * (axisLength + trainWidth) - trainWidth;
      s.flip = goingBack;
      break;
    }
    default: { // DIR_FORWARD, and BOUNCE_IN/BOUNCE_OUT which are handled by the spiral effect itself
      float phase = (float)(nowMs % periodMs) / (float)periodMs;
      s.leadingEdge = phase * (axisLength + trainWidth) - trainWidth;
      s.flip = false;
      break;
    }
  }
  return s;
}

static const uint32_t SWEEP_MIN_PERIOD_MS = 300;
static const uint32_t SWEEP_MAX_PERIOD_MS = 4000;

void effectVerticalSweep(const EffectParams& params, uint32_t nowMs) {
  float bandWidth = params.width * (1.0f / LEDS_PER_RING);
  float trainWidth = bandWidth * (params.palette.count + 2);
  uint32_t periodMs = speedToPeriodMs(params.speedPct, SWEEP_MIN_PERIOD_MS, SWEEP_MAX_PERIOD_MS);
  SweepState s = computeSweepState(params.direction, periodMs, nowMs, trainWidth, 1.0f);

  for (uint8_t ring = 1; ring <= NUM_RINGS; ring++) {
    for (uint8_t pos = 0; pos < LEDS_PER_RING; pos++) {
      float h = ringPosHeight(pos);
      if (s.flip) h = 1.0f - h;
      setRingLED(ring, pos, trainColorAt(h, s.leadingEdge, bandWidth, params.palette));
    }
  }
}

void effectHorizontalSweep(const EffectParams& params, uint32_t nowMs) {
  float bandWidth = params.width * (RING_SPACING_MM / totemLengthMM());
  float trainWidth = bandWidth * (params.palette.count + 2);
  uint32_t periodMs = speedToPeriodMs(params.speedPct, SWEEP_MIN_PERIOD_MS, SWEEP_MAX_PERIOD_MS);
  SweepState s = computeSweepState(params.direction, periodMs, nowMs, trainWidth, 1.0f);

  for (uint8_t ring = 1; ring <= NUM_RINGS; ring++) {
    float x = ringPositionMM(ring) / totemLengthMM();
    if (s.flip) x = 1.0f - x;
    setRing(ring, trainColorAt(x, s.leadingEdge, bandWidth, params.palette));
  }
}

static const uint32_t FLASH_MIN_PERIOD_MS = 150;
static const uint32_t FLASH_MAX_PERIOD_MS = 1500;

void effectAlternateFlash(const EffectParams& params, uint32_t nowMs) {
  uint32_t periodMs = speedToPeriodMs(params.speedPct, FLASH_MIN_PERIOD_MS, FLASH_MAX_PERIOD_MS);
  bool leftOn = (nowMs % (periodMs * 2)) < periodMs;

  CRGB colorLeft = params.palette.colors[0];
  CRGB colorRight = (params.palette.count >= 2) ? params.palette.colors[1] : params.palette.colors[0];

  for (uint8_t ring = 1; ring <= 3; ring++) {
    setRing(ring, leftOn ? colorLeft : CRGB::Black);
  }
  for (uint8_t ring = 4; ring <= 6; ring++) {
    setRing(ring, leftOn ? CRGB::Black : colorRight);
  }
}

static const float SPIRAL_TWISTS = 3.0f; // full rotations end-to-end; a look-and-feel constant

static float spiralCoord(uint8_t ring, uint8_t pos) {
  float axial = ringPositionMM(ring) / totemLengthMM();
  float angular = (float)pos / LEDS_PER_RING;
  return axial + angular * SPIRAL_TWISTS;
}

void effectSpiral(const EffectParams& params, uint32_t nowMs) {
  float bandWidth = params.width * (1.0f / LEDS_PER_RING);
  float axisLength = 1.0f + SPIRAL_TWISTS;
  float trainWidth = bandWidth * (params.palette.count + 2);
  uint32_t periodMs = speedToPeriodMs(params.speedPct, SWEEP_MIN_PERIOD_MS, SWEEP_MAX_PERIOD_MS);

  if (params.direction == DIR_BOUNCE_IN || params.direction == DIR_BOUNCE_OUT) {
    uint32_t cycle = periodMs * 2;
    float t = (float)(nowMs % cycle) / (float)periodMs; // 0..2, triangle 0->1->0
    float triangle = (t < 1.0f) ? t : (2.0f - t);
    float sep = (params.direction == DIR_BOUNCE_OUT) ? triangle : (1.0f - triangle);
    float center = axisLength / 2.0f;
    float leadingEdgeA = (center - sep * center) - trainWidth / 2.0f;
    float leadingEdgeB = (center + sep * center) - trainWidth / 2.0f;

    for (uint8_t ring = 1; ring <= NUM_RINGS; ring++) {
      for (uint8_t pos = 0; pos < LEDS_PER_RING; pos++) {
        float u = spiralCoord(ring, pos);
        CRGB colorA = trainColorAt(u, leadingEdgeA, bandWidth, params.palette);
        CRGB colorB = trainColorAt(u, leadingEdgeB, bandWidth, params.palette);
        setRingLED(ring, pos, colorA + colorB);
      }
    }
    return;
  }

  SweepState s = computeSweepState(params.direction, periodMs, nowMs, trainWidth, axisLength);

  for (uint8_t ring = 1; ring <= NUM_RINGS; ring++) {
    for (uint8_t pos = 0; pos < LEDS_PER_RING; pos++) {
      float u = spiralCoord(ring, pos);
      if (s.flip) u = axisLength - u;
      setRingLED(ring, pos, trainColorAt(u, s.leadingEdge, bandWidth, params.palette));
    }
  }
}

struct SnowFlake {
  bool active;
  uint8_t pos;
  int8_t stepDir;
  uint8_t stepsTaken;
  uint32_t lastStepMs;
  uint32_t nextSpawnMs;
  CRGB color;
};

static SnowFlake snowFlakes[NUM_RINGS];

static const uint32_t SNOW_MIN_STEP_MS = 40;
static const uint32_t SNOW_MAX_STEP_MS = 220;
static const uint32_t SNOW_MIN_GAP_MS = 200;
static const uint32_t SNOW_MAX_GAP_MS = 2500;
static const uint8_t SNOW_FALL_STEPS = LEDS_PER_RING / 2; // top to bottom, one side of the ring

void effectSnow(const EffectParams& params, uint32_t nowMs) {
  uint32_t stepMs = speedToPeriodMs(params.speedPct, SNOW_MIN_STEP_MS, SNOW_MAX_STEP_MS);
  uint32_t gapMs = speedToPeriodMs(params.speedPct, SNOW_MIN_GAP_MS, SNOW_MAX_GAP_MS);

  for (uint8_t ring = 1; ring <= NUM_RINGS; ring++) {
    SnowFlake& f = snowFlakes[ring - 1];

    if (!f.active) {
      if (nowMs >= f.nextSpawnMs) {
        f.active = true;
        f.pos = 0;
        f.stepDir = random(0, 2) ? 1 : -1;
        f.stepsTaken = 0;
        f.lastStepMs = nowMs;
        f.color = params.palette.colors[random(0, params.palette.count)];
      }
    } else if (nowMs - f.lastStepMs >= stepMs) {
      f.pos = (f.pos + f.stepDir + LEDS_PER_RING) % LEDS_PER_RING;
      f.stepsTaken++;
      f.lastStepMs = nowMs;
      if (f.stepsTaken >= SNOW_FALL_STEPS) {
        f.active = false;
        f.nextSpawnMs = nowMs + random(gapMs / 2, gapMs + gapMs / 2);
      }
    }

    setRing(ring, CRGB::Black);
    if (f.active) {
      setRingLED(ring, f.pos, f.color);
    }
  }
}
