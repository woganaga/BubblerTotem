#include "EffectUI.h"

static const DirOption VERTICAL_DIRS[] = {
  { "Down", DIR_FORWARD },
  { "Up", DIR_REVERSE },
  { "Bounce", DIR_BOUNCE },
};
static const DirOption HORIZONTAL_DIRS[] = {
  { "Left", DIR_REVERSE },
  { "Right", DIR_FORWARD },
  { "Bounce", DIR_BOUNCE },
};
static const DirOption CHASE_DIRS[] = {
  { "Forward", DIR_FORWARD },
  { "Reverse", DIR_REVERSE },
  { "Bounce", DIR_BOUNCE },
};
static const DirOption SPIRAL_DIRS[] = {
  { "Left", DIR_REVERSE },
  { "Right", DIR_FORWARD },
  { "Bounce In", DIR_BOUNCE_IN },
  { "Bounce Out", DIR_BOUNCE_OUT },
};
static const DirOption PINWHEEL_DIRS[] = {
  { "Clockwise", DIR_FORWARD },
  { "Counterclockwise", DIR_REVERSE },
};
static const DirOption XL_DIRS[] = {
  { "Forward", DIR_FORWARD },
  { "Reverse", DIR_REVERSE },
  { "Bounce", DIR_BOUNCE },
};
static const DirOption XL_LR_DIRS[] = {
  { "Left", DIR_REVERSE },
  { "Right", DIR_FORWARD },
};
static const DirOption XL_CW_DIRS[] = {
  { "Clockwise", DIR_FORWARD },
  { "Counterclockwise", DIR_REVERSE },
};

const EffectUIInfo EFFECT_UI[EFFECT_COUNT] = {
  { false, false, false, false, false, nullptr, 0 },              // Off
  { true, false, false, false, false, VERTICAL_DIRS, 3 },          // Vertical Sweep
  { true, false, false, false, false, HORIZONTAL_DIRS, 3 },        // Horizontal Sweep
  { false, false, false, true, false, nullptr, 0 },                // Alternate Flash
  { true, false, false, false, true, CHASE_DIRS, 3 },              // Chase
  { true, true, true, false, false, SPIRAL_DIRS, 4 },              // Spiral
  { false, false, false, false, false, nullptr, 0 },              // Snow
  { false, false, false, false, false, PINWHEEL_DIRS, 2 },        // Pinwheel
  { false, false, false, false, false, nullptr, 0 },              // Colorwash
  { false, false, false, false, false, nullptr, 0 },              // Fire
  { false, false, false, false, false, nullptr, 0 },              // Confetti
  { true, false, false, false, false, nullptr, 0 },                // Ripple
  // xLights-derived effects. Trailing xLights-control fields per row.
  { false, false, false, false, false, XL_DIRS, 3,
    "Bars", 6, "Axis (0=totem,1=ring)", 0, 1, nullptr, 0, nullptr, nullptr, "Gradient", "3D" },       // XL Bars
  { false, false, false, false, false, nullptr, 0,
    nullptr, 0, "Circular", 0, 1, nullptr, 0, nullptr, nullptr, "Fade Around", "Fade Along" },        // XL Colorwash
  { false, false, false, false, false, XL_LR_DIRS, 2,
    "Strands", 6, nullptr, 0, 0, nullptr, 0, "Thickness", "Rotation", "3D", nullptr },                // XL Spirals
  { false, false, false, false, false, XL_CW_DIRS, 2,
    "Arms", 8, nullptr, 0, 0, nullptr, 0, "Arm Width", "Twist", "3D", nullptr },                      // XL Pinwheel
  { false, false, false, false, false, nullptr, 0,
    nullptr, 0, "Style", 1, 5, "Chunks", 10, nullptr, nullptr, nullptr, "Palette Colors" },           // XL Butterfly
  { false, false, false, false, false, nullptr, 0,
    nullptr, 0, "Color Style", 0, 3, "Line Density", 16, nullptr, nullptr, nullptr, nullptr },        // XL Plasma
  { true, false, false, false, false, XL_DIRS, 3,
    "Chases", 6, "Fade", 0, 3, nullptr, 0, nullptr, nullptr, nullptr, "Palette Colors" },             // XL SingleStrand
  { true, false, false, false, false, nullptr, 0,
    nullptr, 0, "Path", 0, 2, nullptr, 0, nullptr, nullptr, nullptr, nullptr },                       // XL Morph
  { false, false, false, false, false, nullptr, 0,
    nullptr, 0, nullptr, 0, 0, nullptr, 0, "Flash Length %", nullptr, nullptr, nullptr },             // Beat Flash
};
