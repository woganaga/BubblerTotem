#include "WebUI.h"
#include <WiFi.h>
#include <WebServer.h>
#include "EffectManager.h"

static const char* AP_SSID = "BubblerTotem";
static const char* AP_PASSWORD = "BubbleGlow1"; // change before use if this matters to you

static WebServer server(80);

static void handleRoot() {
  EffectId active = getActiveEffect();

  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Bubbler Totem</title><style>";
  html += "body{font-family:sans-serif;background:#111;color:#eee;text-align:center;padding:2em}";
  html += "a{display:block;margin:0.5em auto;padding:0.75em;max-width:280px;border-radius:8px;";
  html += "background:#333;color:#fff;text-decoration:none;font-size:1.1em}";
  html += "a.active{background:#4caf50}";
  html += "</style></head><body><h1>Bubbler Totem</h1>";

  for (uint8_t i = 0; i < EFFECT_COUNT; i++) {
    html += "<a href='/set?effect=" + String(i) + "' class='" + (i == active ? "active" : "") + "'>";
    html += EFFECT_NAMES[i];
    html += "</a>";
  }

  html += "</body></html>";
  server.send(200, "text/html", html);
}

static void handleSet() {
  if (server.hasArg("effect")) {
    int id = server.arg("effect").toInt();
    if (id >= 0 && id < EFFECT_COUNT) {
      setActiveEffect((EffectId)id);
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void webUIInit() {
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.begin();
}

void webUIHandle() {
  server.handleClient();
}
