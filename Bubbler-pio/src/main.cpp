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
  // On ESP32-S3's native USB CDC (ARDUINO_USB_CDC_ON_BOOT=1, set in
  // platformio.ini), Serial.print()/printf() blocks waiting for a host to
  // actually read the data - with no serial monitor attached, every one of
  // the many BLE debug prints in BleServer.cpp would stall the calling
  // task (the NimBLE host task), which looked exactly like "BLE only works
  // with the monitor open". A 0ms tx timeout makes writes never block: if
  // nobody's reading, the bytes are just dropped instead of stalling.
  Serial.setTxTimeoutMs(0);
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
  bleServerHandle();
  otaHandle();
  audioUpdate(millis());

  if (audioCalibrationActive()) {
    setAll(audioBeatActive() ? CRGB::White : CRGB::Black);
    showAll();
  } else {
    runActiveEffect(millis());
  }
}
