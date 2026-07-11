#pragma once
#include <Arduino.h>

#define WIFI_HOSTNAME "bubblertotem"

// loads the persisted on/off preference (default: off - BLE is the primary
// interface) and, if enabled, starts connecting in station mode with a
// DHCP-assigned IP; non-blocking
void wifiInit();

// turns the WiFi radio on (connecting with the stored credentials) or off
// (disconnecting and powering down the radio), and persists the choice to
// flash so it survives a reboot. Exposed to the BLE Settings toggle.
void wifiSetEnabled(bool enabled);
bool wifiIsEnabled();
bool wifiIsConnected();
String wifiLocalIP(); // "" if not currently connected
