#include <FastLED.h>
#include "Rings.h"
#include "EffectManager.h"
#include "WifiSetup.h"
#include "WebUI.h"
#include "OTAUpdate.h"

void setup() {
  ringsInit();
  wifiInit();
  webUIInit();
  otaInit();
}

void loop() {
  webUIHandle();
  otaHandle();
  runActiveEffect(millis());
}
