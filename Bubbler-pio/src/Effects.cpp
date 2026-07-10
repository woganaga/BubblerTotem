#include "Effects.h"
#include "Rings.h"
#include "Topology.h"
#include "Color.h"

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

// Same as trainColorAt, but the coordinate space wraps seamlessly at
// wrapLength instead of the train fully exiting to black at the edges - used
// for effects that should loop/rotate continuously forever.
static CRGB wrappedTrainColorAt(float pos, float leadingEdge, float bandWidth, float wrapLength, const Palette& palette) {
  CRGB color = trainColorAt(pos, leadingEdge, bandWidth, palette);
  color += trainColorAt(pos + wrapLength, leadingEdge, bandWidth, palette);
  color += trainColorAt(pos - wrapLength, leadingEdge, bandWidth, palette);
  return color;
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
    default: { // DIR_FORWARD
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

// Monotonic S-curve from 0 to 1; sharpness 1 is a straight line, higher
// values flatten near the ends (snappier switch), lower values flatten near
// the middle (lingers longer half-lit, i.e. more overlap).
static float overlapSigmoid(float u, float sharpness) {
  if (u <= 0.0f) return 0.0f;
  if (u >= 1.0f) return 1.0f;
  float p1 = powf(u, sharpness);
  float p2 = powf(1.0f - u, sharpness);
  return p1 / (p1 + p2);
}

void effectAlternateFlash(const EffectParams& params, uint32_t nowMs) {
  uint32_t periodMs = speedToPeriodMs(params.speedPct, FLASH_MIN_PERIOD_MS, FLASH_MAX_PERIOD_MS);
  float phase = (float)(nowMs % (periodMs * 2)) / (float)(periodMs * 2);

  float sharpness = powf(10.0f, (50.0f - (float)params.overlap) / 25.0f);
  float u = (phase < 0.5f) ? (phase * 2.0f) : ((phase - 0.5f) * 2.0f);
  float shaped = overlapSigmoid(u, sharpness);
  float leftNorm = (phase < 0.5f) ? (1.0f - shaped) : shaped;

  uint8_t leftBrightness = (uint8_t)(leftNorm * 255);
  uint8_t rightBrightness = 255 - leftBrightness;

  CRGB colorLeft = params.palette.colors[0];
  CRGB colorRight = (params.palette.count >= 2) ? params.palette.colors[1] : params.palette.colors[0];
  colorLeft.nscale8(leftBrightness);
  colorRight.nscale8(rightBrightness);

  for (uint8_t ring = 1; ring <= 3; ring++) {
    setRing(ring, colorLeft);
  }
  for (uint8_t ring = 4; ring <= 6; ring++) {
    setRing(ring, colorRight);
  }
}

// Unrolls all 6 rings into one path: ring 1 pos 0..26, then ring 2 pos 0..26,
// and so on through ring 6 - a snake chasing along this path wraps fully
// around each ring before advancing to the next.
static float chaseCoord(uint8_t ring, uint8_t pos) {
  return (float)((ring - 1) * LEDS_PER_RING + pos) / (float)NUM_LEDS_TOTAL;
}

void effectChase(const EffectParams& params, uint32_t nowMs) {
  float bandWidth = params.width * (1.0f / NUM_LEDS_TOTAL);
  float trainWidth = bandWidth * (params.palette.count + 2);
  uint32_t periodMs = speedToPeriodMs(params.speedPct, SWEEP_MIN_PERIOD_MS, SWEEP_MAX_PERIOD_MS);
  SweepState s = computeSweepState(params.direction, periodMs, nowMs, trainWidth, 1.0f);

  for (uint8_t ring = 1; ring <= NUM_RINGS; ring++) {
    for (uint8_t pos = 0; pos < LEDS_PER_RING; pos++) {
      float u = chaseCoord(ring, pos);
      float u1 = s.flip ? 1.0f - u : u;
      CRGB color = trainColorAt(u1, s.leadingEdge, bandWidth, params.palette);
      if (params.dualChase) {
        float u2 = s.flip ? u : 1.0f - u; // mirror image, so it starts from the opposite end
        color += trainColorAt(u2, s.leadingEdge, bandWidth, params.palette);
      }
      setRingLED(ring, pos, color);
    }
  }
}

// angularOffset shifts the strand around the ring's circumference (0..1
// fraction of a full turn), used to lay down multiple parallel strands.
static float spiralCoord(uint8_t ring, uint8_t pos, float twists, float angularOffset) {
  float axial = ringPositionMM(ring) / totemLengthMM();
  float angular = (float)pos / LEDS_PER_RING + angularOffset;
  angular -= floorf(angular); // wrap to [0,1) so every strand covers the same u range
  return axial + angular * twists;
}

void effectSpiral(const EffectParams& params, uint32_t nowMs) {
  float twists = (float)params.twists;
  uint8_t rows = params.rows < 1 ? 1 : params.rows;
  float bandWidth = params.width * (1.0f / LEDS_PER_RING);
  float axisLength = 1.0f + twists;
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
        CRGB color = CRGB::Black;
        for (uint8_t r = 0; r < rows; r++) {
          float u = spiralCoord(ring, pos, twists, (float)r / rows);
          color += trainColorAt(u, leadingEdgeA, bandWidth, params.palette);
          color += trainColorAt(u, leadingEdgeB, bandWidth, params.palette);
        }
        setRingLED(ring, pos, color);
      }
    }
    return;
  }

  // continuous rotation: the coordinate space wraps seamlessly at axisLength
  // instead of the strand fully exiting to black and restarting, so it keeps
  // circulating forever
  float phase = (float)(nowMs % periodMs) / (float)periodMs;
  float leadingEdge = (params.direction == DIR_REVERSE ? -phase : phase) * axisLength;
  leadingEdge = fmodf(leadingEdge, axisLength);
  if (leadingEdge < 0.0f) leadingEdge += axisLength;

  for (uint8_t ring = 1; ring <= NUM_RINGS; ring++) {
    for (uint8_t pos = 0; pos < LEDS_PER_RING; pos++) {
      CRGB color = CRGB::Black;
      for (uint8_t r = 0; r < rows; r++) {
        float u = spiralCoord(ring, pos, twists, (float)r / rows);
        color += wrappedTrainColorAt(u, leadingEdge, bandWidth, axisLength, params.palette);
      }
      setRingLED(ring, pos, color);
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

static const uint32_t PINWHEEL_MIN_PERIOD_MS = 300;
static const uint32_t PINWHEEL_MAX_PERIOD_MS = 4000;

void effectPinwheel(const EffectParams& params, uint32_t nowMs) {
  uint32_t periodMs = speedToPeriodMs(params.speedPct, PINWHEEL_MIN_PERIOD_MS, PINWHEEL_MAX_PERIOD_MS);
  float phase = (float)(nowMs % periodMs) / (float)periodMs;
  if (params.direction == DIR_REVERSE) phase = -phase;

  for (uint8_t ring = 1; ring <= NUM_RINGS; ring++) {
    for (uint8_t pos = 0; pos < LEDS_PER_RING; pos++) {
      float u = (float)pos / LEDS_PER_RING + phase;
      u -= floorf(u); // wrap to [0,1)
      setRingLED(ring, pos, paletteBlend(params.palette, u, true));
    }
  }
}

static const uint32_t WASH_MIN_PERIOD_MS = 800;
static const uint32_t WASH_MAX_PERIOD_MS = 8000;

void effectColorwash(const EffectParams& params, uint32_t nowMs) {
  uint32_t periodMs = speedToPeriodMs(params.speedPct, WASH_MIN_PERIOD_MS, WASH_MAX_PERIOD_MS);
  float t = (float)(nowMs % periodMs) / (float)periodMs * params.palette.count;
  int index = (int)t;
  uint8_t frac = (uint8_t)((t - index) * 255);
  CRGB c0 = params.palette.colors[index % params.palette.count];
  CRGB c1 = params.palette.colors[(index + 1) % params.palette.count];
  setAll(blend(c0, c1, frac));
}

static uint8_t fireHeat[NUM_RINGS][LEDS_PER_RING];
static uint32_t fireLastStepMs = 0;

static const uint32_t FIRE_MIN_STEP_MS = 20;
static const uint32_t FIRE_MAX_STEP_MS = 80;

// heat 0 maps to black; heat 255 maps to the last (hottest) palette color,
// with a smooth gradient through the palette in between.
static CRGB heatColor(uint8_t heat, const Palette& palette) {
  float t = (heat / 255.0f) * palette.count;
  int index = (int)t;
  uint8_t frac = (uint8_t)((t - index) * 255);
  CRGB c0 = (index <= 0) ? CRGB::Black : palette.colors[index - 1];
  CRGB c1 = (index >= palette.count) ? palette.colors[palette.count - 1] : palette.colors[index];
  return blend(c0, c1, frac);
}

void effectFire(const EffectParams& params, uint32_t nowMs) {
  uint32_t stepMs = speedToPeriodMs(params.speedPct, FIRE_MIN_STEP_MS, FIRE_MAX_STEP_MS);

  if (nowMs - fireLastStepMs >= stepMs) {
    fireLastStepMs = nowMs;
    for (uint8_t ring = 0; ring < NUM_RINGS; ring++) {
      uint8_t* heat = fireHeat[ring];

      for (uint8_t i = 0; i < LEDS_PER_RING; i++) {
        uint8_t cooling = random(0, 40);
        heat[i] = (cooling >= heat[i]) ? 0 : heat[i] - cooling;
      }
      for (uint8_t i = LEDS_PER_RING - 1; i >= 2; i--) {
        heat[i] = (heat[i - 1] + heat[i - 2] + heat[i - 2]) / 3;
      }
      if (random(0, 255) < 60) {
        uint8_t sparkPos = random(0, 3);
        heat[sparkPos] = qadd8(heat[sparkPos], random(160, 255));
      }
    }
  }

  for (uint8_t ring = 1; ring <= NUM_RINGS; ring++) {
    for (uint8_t pos = 0; pos < LEDS_PER_RING; pos++) {
      setRingLED(ring, pos, heatColor(fireHeat[ring - 1][pos], params.palette));
    }
  }
}

static uint32_t confettiLastStepMs = 0;

static const uint32_t CONFETTI_MIN_STEP_MS = 20;
static const uint32_t CONFETTI_MAX_STEP_MS = 120;

void effectConfetti(const EffectParams& params, uint32_t nowMs) {
  uint32_t stepMs = speedToPeriodMs(params.speedPct, CONFETTI_MIN_STEP_MS, CONFETTI_MAX_STEP_MS);
  if (nowMs - confettiLastStepMs < stepMs) return;
  confettiLastStepMs = nowMs;

  for (uint8_t ring = 1; ring <= NUM_RINGS; ring++) {
    for (uint8_t pos = 0; pos < LEDS_PER_RING; pos++) {
      CRGB c = ringLED(ring, pos);
      c.nscale8(200);
      setRingLED(ring, pos, c);
    }
  }

  uint8_t sparks = 1 + random(0, 3);
  for (uint8_t i = 0; i < sparks; i++) {
    uint8_t ring = random(1, NUM_RINGS + 1);
    uint8_t pos = random(0, LEDS_PER_RING);
    setRingLED(ring, pos, params.palette.colors[random(0, params.palette.count)]);
  }
}

struct Ripple {
  bool active;
  uint32_t startMs;
  CRGB color;
};

static const uint8_t MAX_RIPPLES = 3;
static Ripple ripples[MAX_RIPPLES];
static uint32_t nextRippleSpawnMs = 0;
static uint8_t nextRippleColorIndex = 0;

static const uint32_t RIPPLE_MIN_TRAVEL_MS = 500;
static const uint32_t RIPPLE_MAX_TRAVEL_MS = 3000;
static const uint32_t RIPPLE_MIN_GAP_MS = 200;
static const uint32_t RIPPLE_MAX_GAP_MS = 1200;

// Keep these formulas in lockstep with the effect functions above - each one
// answers "after how many ms does this effect's frame exactly repeat?" for
// the same constants/direction math its render function uses.
float effectNativePeriodMs(EffectId id, const EffectParams& p) {
  switch (id) {
    case EFFECT_VERTICAL_SWEEP:
    case EFFECT_HORIZONTAL_SWEEP:
    case EFFECT_CHASE: {
      float base = (float)speedToPeriodMs(p.speedPct, SWEEP_MIN_PERIOD_MS, SWEEP_MAX_PERIOD_MS);
      return (p.direction == DIR_BOUNCE) ? base * 2.0f : base; // bounce = out and back
    }
    case EFFECT_ALTERNATE_FLASH: // both sides flash once per full cycle (periodMs * 2)
      return (float)speedToPeriodMs(p.speedPct, FLASH_MIN_PERIOD_MS, FLASH_MAX_PERIOD_MS) * 2.0f;
    case EFFECT_SPIRAL: {
      float base = (float)speedToPeriodMs(p.speedPct, SWEEP_MIN_PERIOD_MS, SWEEP_MAX_PERIOD_MS);
      bool bounce = (p.direction == DIR_BOUNCE_IN || p.direction == DIR_BOUNCE_OUT);
      return bounce ? base * 2.0f : base;
    }
    case EFFECT_PINWHEEL:
      return (float)speedToPeriodMs(p.speedPct, PINWHEEL_MIN_PERIOD_MS, PINWHEEL_MAX_PERIOD_MS);
    case EFFECT_COLORWASH:
      return (float)speedToPeriodMs(p.speedPct, WASH_MIN_PERIOD_MS, WASH_MAX_PERIOD_MS);
    default:
      return 0.0f; // Snow/Fire/Confetti/Ripple: stochastic, no fixed loop
  }
}

void effectRipple(const EffectParams& params, uint32_t nowMs) {
  // faster speed means shorter travel time and a shorter gap between ripples
  uint32_t travelMs = speedToPeriodMs(params.speedPct, RIPPLE_MAX_TRAVEL_MS, RIPPLE_MIN_TRAVEL_MS);
  uint32_t gapMs = speedToPeriodMs(params.speedPct, RIPPLE_MAX_GAP_MS, RIPPLE_MIN_GAP_MS);

  if (nowMs >= nextRippleSpawnMs) {
    for (uint8_t i = 0; i < MAX_RIPPLES; i++) {
      if (!ripples[i].active) {
        ripples[i].active = true;
        ripples[i].startMs = nowMs;
        ripples[i].color = params.palette.colors[nextRippleColorIndex % params.palette.count];
        nextRippleColorIndex++;
        nextRippleSpawnMs = nowMs + random(gapMs / 2, gapMs + gapMs / 2);
        break;
      }
    }
  }

  // each ripple is a single band (not a whole multi-color train) so its size
  // stays sensible relative to how far it actually travels from the center
  float bandWidth = params.width * (RING_SPACING_MM / totemLengthMM());
  const float center = 0.5f;

  for (uint8_t ring = 1; ring <= NUM_RINGS; ring++) {
    float x = ringPositionMM(ring) / totemLengthMM();
    CRGB color = CRGB::Black;

    for (uint8_t i = 0; i < MAX_RIPPLES; i++) {
      if (!ripples[i].active) continue;

      float age = (float)(nowMs - ripples[i].startMs) / (float)travelMs;
      if (age >= 1.0f) {
        ripples[i].active = false;
        continue;
      }

      float dist = age * (center + bandWidth);
      Palette single = { { ripples[i].color }, 1 };
      CRGB c1 = trainColorAt(x, center - dist - bandWidth * 1.5f, bandWidth, single);
      CRGB c2 = trainColorAt(x, center + dist - bandWidth * 1.5f, bandWidth, single);
      uint8_t fade = (uint8_t)((1.0f - age) * 255); // dim as the ripple travels outward
      c1.nscale8(fade);
      c2.nscale8(fade);
      color += c1;
      color += c2;
    }

    setRing(ring, color);
  }
}
