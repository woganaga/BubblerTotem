#pragma once
#include "EffectManager.h"

// Describes which generic controls (width/twists/rows/overlap/dualChase/
// direction) and which xLights-style controls (count/style/density/
// thickness/twist/flag/flag2) a given effect type exposes, plus the labels
// to show for them. Shared by every transport that renders a params form
// (WebUI.cpp's HTML fragment, BleServer.cpp's JSON "meta" payload) so they
// can't drift out of sync with each other.

struct DirOption {
  const char* label;
  Direction value;
};

struct EffectUIInfo {
  bool usesWidth;
  bool usesTwists;
  bool usesRows;
  bool usesOverlap;
  bool usesDualChase;
  const DirOption* dirOptions;
  uint8_t dirCount;
  // xLights-effect controls: a non-null label means that control applies.
  const char* countLabel; uint8_t countMax;
  const char* styleLabel; uint8_t styleMin; uint8_t styleMax;
  const char* densityLabel; uint8_t densityMax;
  const char* thicknessLabel;
  const char* twistLabel;
  const char* flagLabel;
  const char* flag2Label;
};

extern const EffectUIInfo EFFECT_UI[EFFECT_COUNT];
