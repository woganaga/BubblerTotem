#pragma once
#include <Arduino.h>
#include <FastLED.h>

// Small helpers shared by every transport that renders the UI or talks
// JSON (WebUI.cpp over HTTP, BleServer.cpp over BLE), so they don't drift.

String colorToHex(CRGB c);
CRGB hexToColor(const String& hex);

// escapes a string for embedding inside a JSON string literal
String jsonEscape(const String& s);
