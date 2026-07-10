#include <Arduino.h>
#include <FastLED.h>
#include "Rings.h"
#include "PaletteStore.h"
#include "CategoryStore.h"
#include "EffectPresetStore.h"
#include "EffectManager.h"
#include "WifiSetup.h"
#include "WebUI.h"
#include "OTAUpdate.h"
#include "AudioInput.h"

void setup() {
  paletteStoreInit();
  categoryStoreInit();
  presetStoreInit();
  effectManagerInit();
  ringsInit();
  wifiInit();
  webUIInit();
  otaInit();
  audioInit();
}

void loop() {
  webUIHandle();
  otaHandle();
  audioUpdate(millis());

  if (audioCalibrationActive()) {
    setAll(audioBeatActive() ? CRGB::White : CRGB::Black);
    showAll();
  } else {
    runActiveEffect(millis());
  }
}
