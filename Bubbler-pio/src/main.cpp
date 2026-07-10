#include <Arduino.h>
#include <FastLED.h>
#include "Rings.h"
#include "PaletteStore.h"
#include "CategoryStore.h"
#include "EffectPresetStore.h"
#include "EffectManager.h"
#include "WifiSetup.h"
#include "WebUI.h"
#include "BleServer.h"
#include "OTAUpdate.h"
#include "AudioInput.h"

void setup() {
  Serial.begin(115200);
  delay(200); // let the USB-serial connection settle before the first print
  Serial.println("\nBubblerTotem booting...");

  paletteStoreInit();
  categoryStoreInit();
  presetStoreInit();
  effectManagerInit();
  ringsInit();
  wifiInit();  // off by default; BLE (below) is the primary interface
  webUIInit();
  bleServerInit();
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
