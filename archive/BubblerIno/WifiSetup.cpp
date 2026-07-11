#include "WifiSetup.h"
#include <WiFi.h>

// This sketch is retired in favor of ../Bubbler-pio (which reads real
// credentials from a gitignored Secrets.h). Real values were removed here
// after they were found still checked into this file's history.
static const char* WIFI_SSID = "your-wifi-ssid";
static const char* WIFI_PASSWORD = "your-wifi-password";

void wifiInit() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WIFI_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}
