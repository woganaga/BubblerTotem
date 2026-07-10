#include "WifiSetup.h"
#include <WiFi.h>
#include "LittleFS.h"
#include "Secrets.h" // WIFI_SSID / WIFI_PASSWORD - gitignored, see Secrets.h.example

static const char* WIFI_ENABLED_FILE = "/wifi_enabled.bin";

static bool enabled = true;

static void loadEnabled() {
  LittleFS.begin(true);
  File f = LittleFS.open(WIFI_ENABLED_FILE, "r");
  if (f) {
    if (f.size() == sizeof(enabled)) f.read((uint8_t*)&enabled, sizeof(enabled));
    f.close();
  }
}

static void saveEnabled() {
  File f = LittleFS.open(WIFI_ENABLED_FILE, "w");
  if (!f) return;
  f.write((const uint8_t*)&enabled, sizeof(enabled));
  f.close();
}

static void applyEnabled() {
  if (enabled) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  } else {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
}

void wifiInit() {
  loadEnabled();
  applyEnabled();
}

void wifiSetEnabled(bool e) {
  enabled = e;
  saveEnabled();
  applyEnabled();
}

bool wifiIsEnabled() { return enabled; }
bool wifiIsConnected() { return WiFi.status() == WL_CONNECTED; }
String wifiLocalIP() { return wifiIsConnected() ? WiFi.localIP().toString() : String(""); }
