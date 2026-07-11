#include "XLFX.h"
#include "Rings.h"
#include "Color.h"
#include <math.h>

// ---- 27x6 scratch buffer (unrolled cylinder) -------------------------------
// xlbuf[y][x]: y = ring index 0..5, x = position around ring 0..26.

static CRGB xlbuf[NUM_RINGS][LEDS_PER_RING];

static inline void xlClear() {
  memset(xlbuf, 0, sizeof(xlbuf));
}

static inline void xlAdd(int x, int y, CRGB c) {
  if (y < 0 || y >= NUM_RINGS) return;
  x = ((x % LEDS_PER_RING) + LEDS_PER_RING) % LEDS_PER_RING; // wrap around the ring
  xlbuf[y][x] += c;
}

static inline void xlSet(int x, int y, CRGB c) {
  if (y < 0 || y >= NUM_RINGS) return;
  x = ((x % LEDS_PER_RING) + LEDS_PER_RING) % LEDS_PER_RING;
  xlbuf[y][x] = c;
}

static void xlCommit() {
  for (uint8_t y = 0; y < NUM_RINGS; y++) {
    for (uint8_t x = 0; x < LEDS_PER_RING; x++) {
      setRingLED(y + 1, x, xlbuf[y][x]);
    }
  }
}

// Color/palette math (blendPct, scaleColor, rainbowHue, paletteBlend) comes
// from the shared Color.h.

// higher speedPct -> more cycles-per-second
static inline float speedCycles(uint8_t speedPct, float scale) {
  return (speedPct / 100.0f) * scale;
}

// ---- Bars ------------------------------------------------------------------
// count=bar repeat, style: 0 along-totem / 1 around-ring, direction FWD/REV/BOUNCE(expand),
// flag=gradient, flag2=3D fade across each bar.

void xlBars(const EffectParams& p, uint32_t nowMs) {
  xlClear();
  int colorcnt = p.palette.count < 1 ? 1 : p.palette.count;
  int barCount = p.count * colorcnt;
  if (barCount < 1) barCount = 1;
  bool around = (p.style == 1);
  int dim = around ? LEDS_PER_RING : NUM_RINGS;
  int barSize = (int)ceilf((float)dim / barCount);
  if (barSize < 1) barSize = 1;
  int blockSize = colorcnt * barSize;

  float pos = fmodf(nowMs / 1000.0f * speedCycles(p.speedPct, 3.0f), 1.0f);
  int offset = (int)(pos * blockSize);
  bool expand = (p.direction == DIR_BOUNCE);
  bool rev = (p.direction == DIR_REVERSE);
  int center = dim / 2;

  for (int y = 0; y < NUM_RINGS; y++) {
    for (int x = 0; x < LEDS_PER_RING; x++) {
      int c = around ? x : y;
      int cc = rev ? (dim - c - 1) : c;
      if (expand) cc = abs(c - center); // mirror out from the center
      int n = cc + offset;
      int colorIdx = (((n / barSize) % colorcnt) + colorcnt) % colorcnt;
      float pct = (float)(((n % barSize) + barSize) % barSize) / barSize;
      CRGB col = p.flag
                     ? blendPct(p.palette.colors[colorIdx], p.palette.colors[(colorIdx + 1) % colorcnt], pct)
                     : p.palette.colors[colorIdx];
      if (p.flag2) col = scaleColor(col, (float)(barSize - (n % barSize) - 1) / barSize); // 3D fade
      xlSet(x, y, col);
    }
  }
  xlCommit();
}

// ---- Color Wash ------------------------------------------------------------
// Whole surface = one palette-blended color, cycling over time.
// flag=hFade (bright ramp around ring), flag2=vFade (bright ramp along totem), style bit0=circular palette.

void xlColorWash(const EffectParams& p, uint32_t nowMs) {
  xlClear();
  bool circular = (p.style & 1);
  float phase = fmodf(nowMs / 1000.0f * speedCycles(p.speedPct, 1.5f), 1.0f);
  CRGB base = paletteBlend(p.palette, phase, circular);

  for (int y = 0; y < NUM_RINGS; y++) {
    for (int x = 0; x < LEDS_PER_RING; x++) {
      CRGB c = base;
      if (p.flag) { // horizontal (around-ring) triangle fade
        float fx = 1.0f - fabsf((float)x / (LEDS_PER_RING - 1) - 0.5f) * 2.0f;
        c = scaleColor(c, fx);
      }
      if (p.flag2) { // vertical (along-totem) triangle fade
        float fy = 1.0f - fabsf((float)y / (NUM_RINGS - 1) - 0.5f) * 2.0f;
        c = scaleColor(c, fy);
      }
      xlSet(x, y, c);
    }
  }
  xlCommit();
}

// ---- Spirals (barber pole) -------------------------------------------------
// count=strand repeat, thickness=strand width %, twistDeg=diagonal rotation,
// direction FWD/REV = movement sign, flag=3D fade across thickness.

void xlSpirals(const EffectParams& p, uint32_t nowMs) {
  xlClear();
  int colorcnt = p.palette.count < 1 ? 1 : p.palette.count;
  int spiralCount = colorcnt * (p.count < 1 ? 1 : p.count);
  float deltaStrands = (float)LEDS_PER_RING / spiralCount;
  float spiralThickness = (deltaStrands * p.thickness / 100.0f) + 1.0f;
  int dir = (p.direction == DIR_REVERSE) ? -1 : 1;
  float rotation = p.twistDeg / 36.0f; // degrees -> columns of shear across the totem
  // wrap the scroll into one ring's worth so the float doesn't lose precision
  // (and go chunky) after long uptime; xlSet wraps x mod LEDS_PER_RING anyway
  float scroll = fmodf(nowMs / 1000.0f * speedCycles(p.speedPct, 8.0f) * LEDS_PER_RING, LEDS_PER_RING);
  float state = scroll * dir;

  for (int ns = 0; ns < spiralCount; ns++) {
    int strandBase = (int)(ns * deltaStrands);
    int colorIdx = ns % colorcnt;
    CRGB color = p.palette.colors[colorIdx];
    int thickN = (int)spiralThickness;
    if (thickN < 1) thickN = 1;
    for (int t = 0; t < thickN; t++) {
      for (int y = 0; y < NUM_RINGS; y++) {
        float xd = strandBase + t + state + y * rotation;
        int x = (int)floorf(xd);
        CRGB c = color;
        if (p.flag) { // 3D fade across the strand thickness
          float f = (dir < 0) ? (float)(t + 1) / thickN : (float)(thickN - t) / thickN;
          c = scaleColor(c, f);
        }
        xlSet(x, y, c);
      }
    }
  }
  xlCommit();
}

// ---- Pinwheel (angular) ----------------------------------------------------
// count=arms, thickness=arm angular width %, twistDeg=twist per totem depth
// (arms spiral into a helix), direction FWD/REV = rotation sense, flag=3D fade.

void xlPinwheel(const EffectParams& p, uint32_t nowMs) {
  xlClear();
  int arms = p.count < 1 ? 1 : p.count;
  int colorcnt = p.palette.count < 1 ? 1 : p.palette.count;
  float degPerArm = 360.0f / arms;
  float armWidth = degPerArm * (p.thickness < 1 ? 25 : p.thickness) / 100.0f;
  int dir = (p.direction == DIR_REVERSE) ? -1 : 1;
  float rot = nowMs / 1000.0f * speedCycles(p.speedPct, 120.0f) * dir;

  for (int y = 0; y < NUM_RINGS; y++) {
    float depth = (NUM_RINGS > 1) ? (float)y / (NUM_RINGS - 1) : 0.0f;
    for (int x = 0; x < LEDS_PER_RING; x++) {
      float theta = (float)x / LEDS_PER_RING * 360.0f + rot + depth * p.twistDeg;
      theta = fmodf(theta, 360.0f);
      if (theta < 0) theta += 360.0f;
      int arm = (int)(theta / degPerArm) % arms;
      float within = fmodf(theta, degPerArm);
      if (within <= armWidth) {
        CRGB c = p.palette.colors[arm % colorcnt];
        if (p.flag) { // fade toward the trailing edge of the arm
          c = scaleColor(c, 1.0f - within / armWidth);
        }
        xlSet(x, y, c);
      }
    }
  }
  xlCommit();
}

// ---- Butterfly -------------------------------------------------------------
// style=1..5 formula variant, density=chunks (posterize), flag2=palette(1)/rainbow(0),
// speed animates the field.

void xlButterfly(const EffectParams& p, uint32_t nowMs) {
  xlClear();
  int W = LEDS_PER_RING, H = NUM_RINGS;
  float sz = W + H;
  float rsz = (1.0f / sz) * (float)(2.0 * PI);
  float offset = nowMs / 1000.0f * speedCycles(p.speedPct, 4.0f);
  int chunks = p.density;
  int skip = 2;
  int style = p.style < 1 ? 1 : p.style;
  bool usePalette = p.flag2;

  int curState = (int)(nowMs / 50.0f * (p.speedPct / 10.0f));
  int maxframe = H * 2;
  int frame = (H * curState / 200) % maxframe;

  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      float fx = x, fy = y, h = 0;
      switch (style) {
        case 2: {
          float f = (frame < H) ? frame + 1 : maxframe - frame;
          float x1 = (fx - W / 2.0f) / f, y1 = (fy - H / 2.0f) / f;
          h = sqrtf(x1 * x1 + y1 * y1);
          break;
        }
        case 3: {
          float f = (frame < maxframe / 2) ? frame + 1 : maxframe - frame;
          f = f * 0.1f + H / 60.0f;
          float x1 = (fx - W / 2.0f) / f, y1 = (fy - H / 2.0f) / f;
          h = sinf(x1) * cosf(y1);
          break;
        }
        case 4: {
          float n = (fx * fx - fy * fy) * sinf(offset + ((fx + fy) * rsz));
          float d = fx * fx + fy * fy;
          h = d > 0.001f ? n / d : 0.0f;
          h = h - floorf(h);
          if (h < 0) h += 1.0f;
          break;
        }
        case 5: {
          float n = fabsf((fx * fx - fy * fy) * sinf(offset + ((fx + fy) * (float)(2.0 * PI) / (H * W))));
          float d = fx * fx + fy * fy;
          h = d > 0.001f ? n / d : 0.0f;
          break;
        }
        default: { // style 1 - classic
          float n = fabsf((fx * fx - fy * fy) * sinf(offset + ((fx + fy) * rsz)));
          float d = fx * fx + fy * fy;
          h = d > 0.001f ? n / d : 0.0f;
          if (h > 1) h = 1;
          break;
        }
      }
      if (chunks > 1 && ((int)(h * chunks) % skip) == 0) continue; // posterize gaps
      xlSet(x, y, usePalette ? paletteBlend(p.palette, h, false) : rainbowHue(h));
    }
  }
  xlCommit();
}

// ---- Plasma ----------------------------------------------------------------
// style=0 palette / 1 red-green / 2 blue-green / 3 rgb-tri, density=line density,
// speed animates.

void xlPlasma(const EffectParams& p, uint32_t nowMs) {
  xlClear();
  int W = LEDS_PER_RING, H = NUM_RINGS;
  float time = nowMs / 1000.0f * speedCycles(p.speedPct, 2.0f);
  float lineDensity = p.density < 1 ? 1 : p.density;
  float sin_time_5 = sinf(time / 5.0f);
  float cos_time_3 = cosf(time / 3.0f);
  float sin_time_2 = sinf(time / 2.0f);
  int style = p.style;

  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      float rx = (W > 1) ? (float)x / (W - 1) : 0.0f;
      float ry = (H > 1) ? (float)y / (H - 1) : 0.0f;
      float cx = rx + 0.5f * sin_time_5;
      float cy = ry + 0.5f * cos_time_3;
      float v = sinf(rx * 10.0f + time);
      v += sinf(10.0f * (rx * sin_time_2 + ry * cos_time_3) + time);
      v += sinf(sqrtf(50.0f * (cx * cx + cy * cy) + time));
      v += sinf(rx + time);
      v += sinf((ry + time) / 2.0f);
      v += sinf((rx + ry + time) / 2.0f);
      v += sinf(sqrtf(rx * rx + ry * ry) + time);
      v = v / 2.0f;
      float vldpi = v * lineDensity * (float)PI;

      CRGB c;
      switch (style) {
        case 1:
          c = CRGB((uint8_t)((sinf(vldpi) + 1.0f) * 127.5f), (uint8_t)((cosf(vldpi) + 1.0f) * 127.5f), 0);
          break;
        case 2:
          c = CRGB(1, (uint8_t)((cosf(vldpi) + 1.0f) * 127.5f), (uint8_t)((sinf(vldpi) + 1.0f) * 127.5f));
          break;
        case 3:
          c = CRGB((uint8_t)((sinf(vldpi) + 1.0f) * 127.5f),
                   (uint8_t)((sinf(vldpi + 2.0f * (float)PI / 3.0f) + 1.0f) * 127.5f),
                   (uint8_t)((sinf(vldpi + 4.0f * (float)PI / 3.0f) + 1.0f) * 127.5f));
          break;
        default: {
          float h = (sinf(vldpi + 2.0f * (float)PI / 3.0f) + 1.0f) * 0.5f;
          c = paletteBlend(p.palette, h, false);
          break;
        }
      }
      xlSet(x, y, c);
    }
  }
  xlCommit();
}

// ---- Single Strand chase ---------------------------------------------------
// Treats the whole totem as one 162-pixel strand. count=number of chases,
// width(1-3)*~11 = chase length, style=fade type (0 none,1 head,2 tail,3 both),
// flag2=rainbow(0)/palette(1), direction FWD/REV/BOUNCE.

void xlSingleStrand(const EffectParams& p, uint32_t nowMs) {
  xlClear();
  int total = NUM_LEDS_TOTAL;
  int chases = p.count < 1 ? 1 : p.count;
  int chaseLen = p.width * (total / 12); // ~13,27,40 px
  if (chaseLen < 1) chaseLen = 1;
  int colorcnt = p.palette.count < 1 ? 1 : p.palette.count;

  float pos = fmodf(nowMs / 1000.0f * speedCycles(p.speedPct, 1.0f), 1.0f);
  if (p.direction == DIR_BOUNCE) { pos *= 2.0f; if (pos > 1.0f) pos = 2.0f - pos; }
  bool rev = (p.direction == DIR_REVERSE);
  float dx = (float)total / chases;

  for (int chase = 0; chase < chases; chase++) {
    int head = (int)(chase * dx + pos * total);
    for (int i = 0; i < chaseLen; i++) {
      int idx = head - i;
      idx = ((idx % total) + total) % total;
      int drawIdx = rev ? (total - idx - 1) : idx;

      CRGB c;
      if (p.flag2) { // palette across the chase length
        int colorIdx = colorcnt == 1 ? 0 : (int)ceilf((float)((chaseLen - i) * colorcnt) / chaseLen) - 1;
        if (colorIdx >= colorcnt) colorIdx = colorcnt - 1;
        if (colorIdx < 0) colorIdx = 0;
        c = p.palette.colors[colorIdx];
      } else {
        c = rainbowHue(1.0f - (float)i / chaseLen); // rainbow tail
      }

      float f = 1.0f;
      switch (p.style) {
        case 1: f = (float)(chaseLen - i) / chaseLen; break;                 // fade from head
        case 2: f = (float)(i + 1) / chaseLen; break;                        // fade from tail
        case 3: f = 1.0f - fabsf((float)i / (chaseLen - 1) - 0.5f) * 2.0f; break; // fade both ends
        default: break;
      }
      c = scaleColor(c, f);
      xlSet(drawIdx % LEDS_PER_RING, drawIdx / LEDS_PER_RING, c);
    }
  }
  xlCommit();
}

// ---- Morph -----------------------------------------------------------------
// A bright line sweeps across the surface from a start edge to an end edge,
// leaving a fading trail. style selects the start/end line geometry,
// width=head thickness, palette[0]=head, palette[last]=tail.

static void morphLine(float ax, float ay, float bx, float by, CRGB color, int thick) {
  // Bresenham-ish over the longer axis, in grid coords (x:0..26, y:0..5)
  int x0 = (int)roundf(ax), y0 = (int)roundf(ay);
  int x1 = (int)roundf(bx), y1 = (int)roundf(by);
  int dx = abs(x1 - x0), dy = abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = (dx > dy ? dx : -dy) / 2;
  for (;;) {
    for (int t = -(thick / 2); t <= thick / 2; t++) {
      xlAdd(x0, y0 + t, color);
    }
    if (x0 == x1 && y0 == y1) break;
    int e2 = err;
    if (e2 > -dx) { err -= dy; x0 += sx; }
    if (e2 < dy) { err += dx; y0 += sy; }
  }
}

void xlMorph(const EffectParams& p, uint32_t nowMs) {
  xlClear();
  int W = LEDS_PER_RING - 1, H = NUM_RINGS - 1;
  int colorcnt = p.palette.count < 1 ? 1 : p.palette.count;
  CRGB head = p.palette.colors[0];
  CRGB tail = p.palette.colors[colorcnt - 1];
  int thick = p.width;

  // start line and end line vary by style
  float sax, say, sbx, sby, eax, eay, ebx, eby;
  switch (p.style) {
    case 1: // horizontal band sweeps top -> bottom
      sax = 0;  say = 0;  sbx = W;  sby = 0;
      eax = 0;  eay = H;  ebx = W;  eby = H;  break;
    case 2: // diagonal sweep
      sax = 0;  say = 0;  sbx = 0;  sby = H;
      eax = W;  eay = 0;  ebx = W;  eby = H;  break;
    default: // vertical band sweeps left -> right around the ring
      sax = 0;  say = 0;  sbx = 0;  sby = H;
      eax = W;  eay = 0;  ebx = W;  eby = H;  break;
  }

  float prog = fmodf(nowMs / 1000.0f * speedCycles(p.speedPct, 0.7f), 1.0f);

  const int TRAIL = 6;
  for (int tstep = TRAIL; tstep >= 0; tstep--) {
    float tp = prog - tstep * 0.06f;
    if (tp < 0) continue;
    float ax = sax + (eax - sax) * tp, ay = say + (eay - say) * tp;
    float bx = sbx + (ebx - sbx) * tp, by = sby + (eby - sby) * tp;
    float fade = (float)(TRAIL - tstep) / (TRAIL + 1);
    CRGB c = (tstep == 0) ? head : scaleColor(tail, fade * 0.7f);
    morphLine(ax, ay, bx, by, c, tstep == 0 ? thick : 1);
  }
  xlCommit();
}
