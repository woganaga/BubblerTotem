#include "OTAUpdate.h"
#include "WifiSetup.h"
#include <ArduinoOTA.h>

void otaInit() {
  ArduinoOTA.setPassword("mila");
  ArduinoOTA.setHostname(WIFI_HOSTNAME);
  ArduinoOTA.begin();
}

void otaHandle() {
  ArduinoOTA.handle();
}
