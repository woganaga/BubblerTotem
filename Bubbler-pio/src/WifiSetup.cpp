#include "WifiSetup.h"
#include <WiFi.h>

static const char* WIFI_SSID = "***REMOVED-SSID***";
static const char* WIFI_PASSWORD = "***REMOVED***";

void wifiInit() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WIFI_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}
