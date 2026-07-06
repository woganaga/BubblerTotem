#include "WebUI.h"
#include <WebServer.h>
#include <Update.h>
#include "EffectManager.h"

static WebServer server(80);

struct DirOption {
  const char* label;
  Direction value;
};

static const DirOption VERTICAL_DIRS[] = {
  { "Down", DIR_FORWARD },
  { "Up", DIR_REVERSE },
  { "Bounce", DIR_BOUNCE },
};
static const DirOption HORIZONTAL_DIRS[] = {
  { "Left", DIR_REVERSE },
  { "Right", DIR_FORWARD },
  { "Bounce", DIR_BOUNCE },
};
static const DirOption SPIRAL_DIRS[] = {
  { "Left", DIR_REVERSE },
  { "Right", DIR_FORWARD },
  { "Bounce In", DIR_BOUNCE_IN },
  { "Bounce Out", DIR_BOUNCE_OUT },
};

struct EffectUIInfo {
  bool usesWidth;
  const DirOption* dirOptions;
  uint8_t dirCount;
};

static const EffectUIInfo EFFECT_UI[EFFECT_COUNT] = {
  { false, nullptr, 0 },                  // Off
  { true, VERTICAL_DIRS, 3 },              // Vertical Sweep
  { true, HORIZONTAL_DIRS, 3 },            // Horizontal Sweep
  { false, nullptr, 0 },                  // Alternate Flash
  { true, SPIRAL_DIRS, 4 },                // Spiral
  { false, nullptr, 0 },                  // Snow
};

static String colorToHex(CRGB c) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
  return String(buf);
}

static CRGB hexToColor(const String& hex) {
  long value = strtol(hex.c_str() + 1, nullptr, 16);
  return CRGB((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
}

static const char* PAGE_STYLE =
  "body{font-family:sans-serif;background:#111;color:#eee;text-align:center;padding:2em}"
  "a.button{display:block;margin:0.5em auto;padding:0.75em;max-width:280px;border-radius:8px;"
  "background:#333;color:#fff;text-decoration:none;font-size:1.1em}"
  "a.button.active{background:#4caf50}"
  "form{max-width:320px;margin:1em auto;text-align:left}"
  "form label{display:block;margin:0.75em 0}"
  "input[type=color]{width:2em;height:2em;border:none;margin-right:0.25em}"
  "input[type=submit]{margin-top:1em;padding:0.5em 1.5em}"
  "a.link{color:#8cf}";

static void appendParamsForm(String& html) {
  EffectId active = getActiveEffect();
  if (active == EFFECT_OFF) return;

  const EffectUIInfo& info = EFFECT_UI[active];
  EffectParams& p = effectParamsFor(active);

  html += "<form method='POST' action='/params'>";
  html += "<h2>" + String(EFFECT_NAMES[active]) + " settings</h2>";

  html += "<label>Colors <select name='count'>";
  for (uint8_t i = 1; i <= MAX_PALETTE_COLORS; i++) {
    html += "<option value='" + String(i) + "'" + (i == p.palette.count ? " selected" : "") + ">" + String(i) + "</option>";
  }
  html += "</select></label>";

  html += "<label>";
  for (uint8_t i = 0; i < MAX_PALETTE_COLORS; i++) {
    CRGB c = (i < p.palette.count) ? p.palette.colors[i] : CRGB::Black;
    html += "<input type='color' name='c" + String(i) + "' value='" + colorToHex(c) + "'>";
  }
  html += "</label>";

  html += "<label>Speed <input type='range' name='speed' min='1' max='100' value='" + String(p.speedPct) + "'></label>";

  if (info.usesWidth) {
    html += "<label>Width <select name='width'>";
    for (uint8_t w = 1; w <= 3; w++) {
      html += "<option value='" + String(w) + "'" + (w == p.width ? " selected" : "") + ">" + String(w) + "</option>";
    }
    html += "</select></label>";
  }

  if (info.dirOptions != nullptr) {
    html += "<label>Direction <select name='direction'>";
    for (uint8_t i = 0; i < info.dirCount; i++) {
      bool sel = info.dirOptions[i].value == p.direction;
      html += "<option value='" + String((int)info.dirOptions[i].value) + "'" + (sel ? " selected" : "") + ">" + info.dirOptions[i].label + "</option>";
    }
    html += "</select></label>";
  }

  html += "<input type='submit' value='Apply'>";
  html += "</form>";
}

static void handleRoot() {
  EffectId active = getActiveEffect();

  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Bubbler Totem</title><style>";
  html += PAGE_STYLE;
  html += "</style></head><body><h1>Bubbler Totem</h1>";

  for (uint8_t i = 0; i < EFFECT_COUNT; i++) {
    html += "<a href='/set?effect=" + String(i) + "' class='button" + (i == active ? " active" : "") + "'>";
    html += EFFECT_NAMES[i];
    html += "</a>";
  }

  appendParamsForm(html);

  html += "<a class='link' href='/update'>Firmware Update</a>";
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

static void handleParams() {
  EffectId active = getActiveEffect();
  if (active != EFFECT_OFF) {
    EffectParams& p = effectParamsFor(active);

    if (server.hasArg("count")) {
      int count = server.arg("count").toInt();
      if (count < 1) count = 1;
      if (count > MAX_PALETTE_COLORS) count = MAX_PALETTE_COLORS;
      p.palette.count = count;
    }
    for (uint8_t i = 0; i < p.palette.count; i++) {
      String argName = "c" + String(i);
      if (server.hasArg(argName)) {
        p.palette.colors[i] = hexToColor(server.arg(argName));
      }
    }
    if (server.hasArg("speed")) {
      int speed = server.arg("speed").toInt();
      if (speed < 1) speed = 1;
      if (speed > 100) speed = 100;
      p.speedPct = speed;
    }
    if (server.hasArg("width")) {
      int width = server.arg("width").toInt();
      if (width < 1) width = 1;
      if (width > 3) width = 3;
      p.width = width;
    }
    if (server.hasArg("direction")) {
      p.direction = (Direction)server.arg("direction").toInt();
    }
  }

  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleUpdatePage() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Firmware Update</title><style>";
  html += PAGE_STYLE;
  html += "</style></head><body><h1>Firmware Update</h1>";
  html += "<form method='POST' action='/update' enctype='multipart/form-data'>";
  html += "<input type='file' name='firmware' accept='.bin'>";
  html += "<input type='submit' value='Upload'>";
  html += "</form><a class='link' href='/'>Back</a></body></html>";
  server.send(200, "text/html", html);
}

static void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    Update.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    Update.end(true);
  }
}

static void handleUpdateResult() {
  bool ok = !Update.hasError();
  server.send(200, "text/plain", ok ? "Update OK, rebooting..." : "Update failed");
  if (ok) {
    delay(500);
    ESP.restart();
  }
}

void webUIInit() {
  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/params", HTTP_POST, handleParams);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateResult, handleUpdateUpload);
  server.begin();
}

void webUIHandle() {
  server.handleClient();
}
