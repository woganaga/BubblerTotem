#include "WebUI.h"
#include <WebServer.h>
#include <Update.h>
#include "EffectManager.h"
#include "AudioInput.h"

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
static const DirOption CHASE_DIRS[] = {
  { "Forward", DIR_FORWARD },
  { "Reverse", DIR_REVERSE },
  { "Bounce", DIR_BOUNCE },
};
static const DirOption SPIRAL_DIRS[] = {
  { "Left", DIR_REVERSE },
  { "Right", DIR_FORWARD },
  { "Bounce In", DIR_BOUNCE_IN },
  { "Bounce Out", DIR_BOUNCE_OUT },
};
static const DirOption PINWHEEL_DIRS[] = {
  { "Clockwise", DIR_FORWARD },
  { "Counterclockwise", DIR_REVERSE },
};

struct EffectUIInfo {
  bool usesWidth;
  bool usesTwists;
  bool usesRows;
  bool usesOverlap;
  bool usesDualChase;
  const DirOption* dirOptions;
  uint8_t dirCount;
};

static const EffectUIInfo EFFECT_UI[EFFECT_COUNT] = {
  { false, false, false, false, false, nullptr, 0 },              // Off
  { true, false, false, false, false, VERTICAL_DIRS, 3 },          // Vertical Sweep
  { true, false, false, false, false, HORIZONTAL_DIRS, 3 },        // Horizontal Sweep
  { false, false, false, true, false, nullptr, 0 },                // Alternate Flash
  { true, false, false, false, true, CHASE_DIRS, 3 },              // Chase
  { true, true, true, false, false, SPIRAL_DIRS, 4 },              // Spiral
  { false, false, false, false, false, nullptr, 0 },              // Snow
  { false, false, false, false, false, PINWHEEL_DIRS, 2 },        // Pinwheel
  { false, false, false, false, false, nullptr, 0 },              // Colorwash
  { false, false, false, false, false, nullptr, 0 },              // Fire
  { false, false, false, false, false, nullptr, 0 },              // Confetti
  { true, false, false, false, false, nullptr, 0 },                // Ripple
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
  "a.link{color:#8cf}"
  "#vu{width:280px;height:20px;background:#333;border-radius:4px;margin:1em auto;overflow:hidden}"
  "#vuBar{height:100%;width:0%;background:#4caf50}"
  "#beatDot{width:20px;height:20px;border-radius:50%;background:#333;margin:0.5em auto}"
  "#beatDot.on{background:#ff5252}";

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
  html += "<label>Intensity <input type='range' name='intensity' min='1' max='100' value='" + String(p.intensity) + "'></label>";

  if (info.usesWidth) {
    html += "<label>Width <select name='width'>";
    for (uint8_t w = 1; w <= 3; w++) {
      html += "<option value='" + String(w) + "'" + (w == p.width ? " selected" : "") + ">" + String(w) + "</option>";
    }
    html += "</select></label>";
  }

  if (info.usesTwists) {
    html += "<label>Twists <input type='range' name='twists' min='1' max='10' value='" + String(p.twists) + "'></label>";
  }

  if (info.usesRows) {
    html += "<label>Rows <input type='range' name='rows' min='1' max='6' value='" + String(p.rows) + "'></label>";
  }

  if (info.usesOverlap) {
    html += "<label>Overlap <input type='range' name='overlap' min='1' max='100' value='" + String(p.overlap) + "'></label>";
  }

  if (info.usesDualChase) {
    html += "<label><input type='checkbox' name='dualChase' value='1'" + String(p.dualChase ? " checked" : "") + "> Dual chase (start from both ends)</label>";
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

// drivesLeds marks this page's poll as a heartbeat that also flashes the
// physical LEDs on the beat (used only by the mic settings page)
static void appendVuMeter(String& html, bool drivesLeds) {
  html += "<div id='vu'><div id='vuBar'></div></div>";
  html += "<div id='beatDot'></div>";
  html += "<script>setInterval(function(){"
          "fetch('";
  html += drivesLeds ? "/audio?mic=1" : "/audio";
  html += "').then(function(r){return r.json();}).then(function(d){"
          "document.getElementById('vuBar').style.width=(d.level*100)+'%';"
          "document.getElementById('beatDot').className=d.beat?'on':'';"
          "});},150);</script>";
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
  appendVuMeter(html, false);

  html += "<a class='link' href='/mic'>Mic Settings</a> ";
  html += "<a class='link' href='/update'>Firmware Update</a>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

static void handleAudio() {
  if (server.hasArg("mic")) {
    audioMarkMicPageActive(millis());
  }
  String json = "{\"level\":" + String(audioLevel(), 3) + ",\"beat\":" + (audioBeatActive() ? "true" : "false") + "}";
  server.send(200, "application/json", json);
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
    if (server.hasArg("intensity")) {
      int intensity = server.arg("intensity").toInt();
      if (intensity < 1) intensity = 1;
      if (intensity > 100) intensity = 100;
      p.intensity = intensity;
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
    if (server.hasArg("twists")) {
      int twists = server.arg("twists").toInt();
      if (twists < 1) twists = 1;
      if (twists > 10) twists = 10;
      p.twists = twists;
    }
    if (server.hasArg("rows")) {
      int rows = server.arg("rows").toInt();
      if (rows < 1) rows = 1;
      if (rows > 6) rows = 6;
      p.rows = rows;
    }
    if (server.hasArg("overlap")) {
      int overlap = server.arg("overlap").toInt();
      if (overlap < 1) overlap = 1;
      if (overlap > 100) overlap = 100;
      p.overlap = overlap;
    }
    if (EFFECT_UI[active].usesDualChase) {
      p.dualChase = server.hasArg("dualChase") ? 1 : 0;
    }
  }

  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleMicPage() {
  AudioSettings& s = audioSettings();

  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Mic Settings</title><style>";
  html += PAGE_STYLE;
  html += "</style></head><body><h1>Mic Settings</h1>";

  appendVuMeter(html, true);

  html += "<form method='POST' action='/mic/save'>";
  html += "<label>Sensitivity <input type='range' name='gain' min='1' max='100' value='" + String(s.gain) + "'></label>";
  html += "<label>Noise Floor <input type='range' name='noiseFloor' min='0' max='100' value='" + String(s.noiseFloor) + "'></label>";
  html += "<label>Beat Threshold % <input type='range' name='beatThreshold' min='110' max='400' value='" + String(s.beatThreshold) + "'></label>";
  html += "<label>Beat Debounce (ms) <input type='range' name='beatDebounce' min='30' max='500' value='" + String(s.beatDebounceMs) + "'></label>";
  html += "<input type='submit' value='Apply (save to flash)'>";
  html += "</form>";

  // sliders take effect immediately as they're dragged; Apply only persists
  // the already-live values to flash so they survive a reboot
  html += "<script>(function(){"
          "var form=document.querySelector('form');"
          "function liveUpdate(){fetch('/mic',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
          "body:new URLSearchParams(new FormData(form)).toString()});}"
          "form.querySelectorAll(\"input[type='range']\").forEach(function(el){el.addEventListener('input',liveUpdate);});"
          "})();</script>";

  html += "<a class='link' href='/'>Back</a></body></html>";
  server.send(200, "text/html", html);
}

static void applyMicParamsFromRequest() {
  AudioSettings& s = audioSettings();

  if (server.hasArg("gain")) {
    int gain = server.arg("gain").toInt();
    if (gain < 1) gain = 1;
    if (gain > 100) gain = 100;
    s.gain = gain;
  }
  if (server.hasArg("noiseFloor")) {
    int noiseFloor = server.arg("noiseFloor").toInt();
    if (noiseFloor < 0) noiseFloor = 0;
    if (noiseFloor > 100) noiseFloor = 100;
    s.noiseFloor = noiseFloor;
  }
  if (server.hasArg("beatThreshold")) {
    int beatThreshold = server.arg("beatThreshold").toInt();
    if (beatThreshold < 110) beatThreshold = 110;
    if (beatThreshold > 400) beatThreshold = 400;
    s.beatThreshold = beatThreshold;
  }
  if (server.hasArg("beatDebounce")) {
    int beatDebounce = server.arg("beatDebounce").toInt();
    if (beatDebounce < 30) beatDebounce = 30;
    if (beatDebounce > 500) beatDebounce = 500;
    s.beatDebounceMs = beatDebounce;
  }
}

// called live on every slider drag; applies immediately but doesn't touch flash
static void handleMicParams() {
  applyMicParamsFromRequest();
  server.send(204);
}

// called by the Apply button; applies the submitted values and persists them
static void handleMicSave() {
  applyMicParamsFromRequest();
  audioSaveSettings();
  server.sendHeader("Location", "/mic");
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
  server.on("/audio", HTTP_GET, handleAudio);
  server.on("/mic", HTTP_GET, handleMicPage);
  server.on("/mic", HTTP_POST, handleMicParams);
  server.on("/mic/save", HTTP_POST, handleMicSave);
  server.begin();
}

void webUIHandle() {
  server.handleClient();
}
