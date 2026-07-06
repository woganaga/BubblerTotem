#include <FastLED.h>
#include "Rings.h"
#include "EffectManager.h"
#include "WebUI.h"

void setup() {
  ringsInit();
  webUIInit();
}

void loop() {
  webUIHandle();
  runActiveEffect(millis());
}
