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
static const DirOption XL_DIRS[] = {
  { "Forward", DIR_FORWARD },
  { "Reverse", DIR_REVERSE },
  { "Bounce", DIR_BOUNCE },
};
static const DirOption XL_LR_DIRS[] = {
  { "Left", DIR_REVERSE },
  { "Right", DIR_FORWARD },
};
static const DirOption XL_CW_DIRS[] = {
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
  // xLights-effect controls: a non-null label renders that control.
  const char* countLabel; uint8_t countMax;
  const char* styleLabel; uint8_t styleMin; uint8_t styleMax;
  const char* densityLabel; uint8_t densityMax;
  const char* thicknessLabel;
  const char* twistLabel;
  const char* flagLabel;
  const char* flag2Label;
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
  // xLights-derived effects. Trailing xLights-control fields per row.
  { false, false, false, false, false, XL_DIRS, 3,
    "Bars", 6, "Axis (0=totem,1=ring)", 0, 1, nullptr, 0, nullptr, nullptr, "Gradient", "3D" },       // XL Bars
  { false, false, false, false, false, nullptr, 0,
    nullptr, 0, "Circular", 0, 1, nullptr, 0, nullptr, nullptr, "Fade Around", "Fade Along" },        // XL Colorwash
  { false, false, false, false, false, XL_LR_DIRS, 2,
    "Strands", 6, nullptr, 0, 0, nullptr, 0, "Thickness", "Rotation", "3D", nullptr },                // XL Spirals
  { false, false, false, false, false, XL_CW_DIRS, 2,
    "Arms", 8, nullptr, 0, 0, nullptr, 0, "Arm Width", "Twist", "3D", nullptr },                      // XL Pinwheel
  { false, false, false, false, false, nullptr, 0,
    nullptr, 0, "Style", 1, 5, "Chunks", 10, nullptr, nullptr, nullptr, "Palette Colors" },           // XL Butterfly
  { false, false, false, false, false, nullptr, 0,
    nullptr, 0, "Color Style", 0, 3, "Line Density", 16, nullptr, nullptr, nullptr, nullptr },        // XL Plasma
  { true, false, false, false, false, XL_DIRS, 3,
    "Chases", 6, "Fade", 0, 3, nullptr, 0, nullptr, nullptr, nullptr, "Palette Colors" },             // XL SingleStrand
  { true, false, false, false, false, nullptr, 0,
    nullptr, 0, "Path", 0, 2, nullptr, 0, nullptr, nullptr, nullptr, nullptr },                       // XL Morph
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

// If the request has this arg, parse+clamp it to [min,max] into out and return
// true; otherwise leave out untouched and return false.
static bool argInt(const char* name, int min, int max, int& out) {
  if (!server.hasArg(name)) return false;
  int v = server.arg(name).toInt();
  if (v < min) v = min;
  if (v > max) v = max;
  out = v;
  return true;
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
  "#beatDot.on{background:#ff5252}"
  ".band{width:280px;height:16px;background:#333;border-radius:4px;margin:0.3em auto;overflow:hidden}"
  ".band>span{display:block;height:100%;width:0%}"
  "#bassBar{background:#ff5252}#midBar{background:#4caf50}#trebBar{background:#8cf}"
  "#bpm{font-size:2em;margin:0.2em}#conf{color:#999;font-size:0.9em}"
  ".tbtn{display:inline-block;padding:0.5em 1em;margin:0.25em;border-radius:8px;background:#333;color:#fff;border:none;font-size:1em;cursor:pointer}";

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

  if (info.countLabel) {
    html += "<label>" + String(info.countLabel) + " <input type='range' name='elemcount' min='1' max='" + String(info.countMax) + "' value='" + String(p.count) + "'></label>";
  }
  if (info.styleLabel) {
    html += "<label>" + String(info.styleLabel) + " <input type='range' name='style' min='" + String(info.styleMin) + "' max='" + String(info.styleMax) + "' value='" + String(p.style) + "'></label>";
  }
  if (info.densityLabel) {
    html += "<label>" + String(info.densityLabel) + " <input type='range' name='density' min='1' max='" + String(info.densityMax) + "' value='" + String(p.density) + "'></label>";
  }
  if (info.thicknessLabel) {
    html += "<label>" + String(info.thicknessLabel) + " <input type='range' name='thickness' min='0' max='100' value='" + String(p.thickness) + "'></label>";
  }
  if (info.twistLabel) {
    html += "<label>" + String(info.twistLabel) + " <input type='range' name='twist' min='-360' max='360' value='" + String(p.twistDeg) + "'></label>";
  }
  if (info.flagLabel) {
    html += "<label><input type='checkbox' name='flag' value='1'" + String(p.flag ? " checked" : "") + "> " + String(info.flagLabel) + "</label>";
  }
  if (info.flag2Label) {
    html += "<label><input type='checkbox' name='flag2' value='1'" + String(p.flag2 ? " checked" : "") + "> " + String(info.flag2Label) + "</label>";
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
  AudioFeatures f = audioFeatures();
  String json = "{";
  json += "\"level\":" + String(f.volume, 3);
  json += ",\"beat\":" + String(audioBeatActive() ? "true" : "false");
  json += ",\"bass\":" + String(f.bass, 3);
  json += ",\"mid\":" + String(f.mid, 3);
  json += ",\"treble\":" + String(f.treble, 3);
  json += ",\"bpm\":" + String((int)(f.bpm + 0.5f));
  json += ",\"conf\":" + String(f.confidence, 2);
  json += "}";
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
    int v;

    if (argInt("count", 1, MAX_PALETTE_COLORS, v)) p.palette.count = v;
    for (uint8_t i = 0; i < p.palette.count; i++) {
      String argName = "c" + String(i);
      if (server.hasArg(argName)) p.palette.colors[i] = hexToColor(server.arg(argName));
    }
    if (argInt("speed", 1, 100, v)) p.speedPct = v;
    if (argInt("intensity", 1, 100, v)) p.intensity = v;
    if (argInt("width", 1, 3, v)) p.width = v;
    if (server.hasArg("direction")) p.direction = (Direction)server.arg("direction").toInt();
    if (argInt("twists", 1, 10, v)) p.twists = v;
    if (argInt("rows", 1, 6, v)) p.rows = v;
    if (argInt("overlap", 1, 100, v)) p.overlap = v;
    if (EFFECT_UI[active].usesDualChase) p.dualChase = server.hasArg("dualChase") ? 1 : 0;

    // xLights-effect controls
    if (argInt("elemcount", 1, 12, v)) p.count = v;
    if (argInt("style", 0, 9, v)) p.style = v;
    if (argInt("density", 1, 32, v)) p.density = v;
    if (argInt("thickness", 0, 100, v)) p.thickness = v;
    if (argInt("twist", -360, 360, v)) p.twistDeg = v;
    if (EFFECT_UI[active].flagLabel) p.flag = server.hasArg("flag") ? 1 : 0;
    if (EFFECT_UI[active].flag2Label) p.flag2 = server.hasArg("flag2") ? 1 : 0;
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

  // live meters + tempo (single poll drives everything and marks the mic heartbeat)
  html += "<div id='vu'><div id='vuBar'></div></div>";
  html += "<div id='beatDot'></div>";
  html += "<div class='band'><span id='bassBar'></span></div><small>Bass</small>";
  html += "<div class='band'><span id='midBar'></span></div><small>Mid</small>";
  html += "<div class='band'><span id='trebBar'></span></div><small>Treble</small>";
  html += "<div id='bpm'>-- BPM</div><div id='conf'>lock: --</div>";
  html += "<button class='tbtn' onclick=\"nudge(-1)\">&divide;2</button>";
  html += "<button class='tbtn' onclick=\"tap()\">TAP</button>";
  html += "<button class='tbtn' onclick=\"nudge(1)\">&times;2</button>";
  html += "<script>"
          "function tap(){fetch('/mic/tap',{method:'POST'});}"
          "function nudge(d){fetch('/mic/nudge?dir='+d,{method:'POST'});}"
          "setInterval(function(){fetch('/audio?mic=1').then(function(r){return r.json();}).then(function(d){"
          "document.getElementById('vuBar').style.width=(d.level*100)+'%';"
          "document.getElementById('beatDot').className=d.beat?'on':'';"
          "document.getElementById('bassBar').style.width=(d.bass*100)+'%';"
          "document.getElementById('midBar').style.width=(d.mid*100)+'%';"
          "document.getElementById('trebBar').style.width=(d.treble*100)+'%';"
          "document.getElementById('bpm').textContent=(d.bpm>0?d.bpm:'--')+' BPM';"
          "document.getElementById('conf').textContent='lock: '+Math.round(d.conf*100)+'%';"
          "});},150);</script>";

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
  int v;
  if (argInt("gain", 1, 100, v)) s.gain = v;
  if (argInt("noiseFloor", 0, 100, v)) s.noiseFloor = v;
  if (argInt("beatThreshold", 110, 400, v)) s.beatThreshold = v;
  if (argInt("beatDebounce", 30, 500, v)) s.beatDebounceMs = v;
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

static void handleMicTap() {
  audioTap();
  audioMarkMicPageActive(millis());
  server.send(204);
}

static void handleMicNudge() {
  int dir = server.hasArg("dir") ? server.arg("dir").toInt() : 1;
  audioTempoNudge(dir);
  server.send(204);
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
  server.on("/mic/tap", HTTP_POST, handleMicTap);
  server.on("/mic/nudge", HTTP_POST, handleMicNudge);
  server.begin();
}

void webUIHandle() {
  server.handleClient();
}
