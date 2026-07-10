#include "WebUI.h"
#include <WebServer.h>
#include <Update.h>
#include <LittleFS.h>
#include "EffectManager.h"
#include "AudioInput.h"
#include "PaletteStore.h"
#include "CategoryStore.h"
#include "EffectPresetStore.h"
#include "EffectUI.h"
#include "WebCommon.h"
#include "WebApp.h"

static WebServer server(80);

// escapes free-form, user-typed names (palette/category/preset) before they're
// dropped into fragment HTML, so a name like `<script>` can't inject markup
static String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
  return out;
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

// ---- fragments (rendered server-side, injected into #content by the app shell) ----

static void renderMainFragment(String& html) {
  html += "<h1>Main</h1>";
  bool beatSync = getBeatSyncEnabled();
  html += "<div class='row'><div>Beat Sync</div>";
  html += "<button class='tbtn" + String(beatSync ? " active" : "") + "' data-action='/action/beatsync?enabled=" + String(beatSync ? 0 : 1) + "'>";
  html += beatSync ? "On" : "Off";
  html += "</button></div>";
  html += "<div class='stub'>When on, the running effect restarts at every "
          "detected beat, syncing its animation to the tempo. Category-based "
          "show mode is coming soon here.</div>";
}

static void renderEffectParamsForm(String& html) {
  EffectId active = getActiveEffect();
  if (active == EFFECT_OFF) return;

  const EffectUIInfo& info = EFFECT_UI[active];
  EffectParams& p = effectParamsFor(active);

  html += "<form data-live='/action/effect/params'>";
  html += "<h2>" + String(EFFECT_NAMES[active]) + " settings</h2>";

  uint8_t linkedPalette = getEffectPaletteId(active);
  html += "<label>Palette <select name='paletteId'>";
  html += "<option value='none'" + String(linkedPalette == PALETTE_ID_NONE ? " selected" : "") + ">(Custom / unlinked)</option>";
  uint8_t paletteIds[MAX_PALETTES];
  uint8_t paletteN = paletteListIds(paletteIds, MAX_PALETTES);
  for (uint8_t i = 0; i < paletteN; i++) {
    const NamedPalette* np = paletteGet(paletteIds[i]);
    if (!np) continue;
    bool sel = paletteIds[i] == linkedPalette;
    html += "<option value='" + String(paletteIds[i]) + "'" + (sel ? " selected" : "") + ">" + htmlEscape(np->name) + "</option>";
  }
  html += "</select></label>";

  html += "<div class='swatches'>";
  for (uint8_t i = 0; i < p.palette.count; i++) {
    html += "<span style='background:" + colorToHex(p.palette.colors[i]) + "'></span>";
  }
  html += "</div>";

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

  html += "</form>";
}

// lets the user name the effect's current live configuration (type, palette
// link, and params) and save it as a new preset, or update the preset it
// was loaded from (see EffectPresetStore.h / the Saved Effects tab)
static void renderSaveEffectForm(String& html) {
  EffectId active = getActiveEffect();
  if (active == EFFECT_OFF) return;

  uint8_t presetId = getActivePresetId();
  const EffectPreset* current = (presetId != PRESET_ID_NONE) ? presetGet(presetId) : nullptr;

  html += "<form data-save='/action/effect/save'>";
  html += "<h2>Save this configuration</h2>";
  if (current) html += "<input type='hidden' name='id' value='" + String(presetId) + "'>";
  html += "<label>Name <input type='text' name='name' maxlength='" + String(PRESET_NAME_LEN - 1) + "' value='" + (current ? htmlEscape(current->name) : "") + "' required></label>";

  html += "<label>Category <select name='categoryId'>";
  html += "<option value=''>(none)</option>";
  uint8_t catIds[MAX_CATEGORIES];
  uint8_t catN = categoryListIds(catIds, MAX_CATEGORIES);
  for (uint8_t i = 0; i < catN; i++) {
    const Category* cat = categoryGet(catIds[i]);
    if (!cat) continue;
    bool sel = current && current->categoryId == catIds[i];
    html += "<option value='" + String(catIds[i]) + "'" + (sel ? " selected" : "") + ">" + htmlEscape(cat->name) + "</option>";
  }
  html += "</select></label>";
  html += "<label>Or new category <input type='text' name='newCategory' maxlength='" + String(CATEGORY_NAME_LEN - 1) + "' placeholder='e.g. Party'></label>";

  html += "<input type='submit' value='" + String(current ? "Update saved effect" : "Save as new") + "'>";
  html += "</form>";
}

static void renderEffectsFragment(String& html) {
  EffectId active = getActiveEffect();
  html += "<h1>Effects</h1>";
  for (uint8_t i = 0; i < EFFECT_COUNT; i++) {
    html += "<button class='button" + String(i == active ? " active" : "") + "' data-action='/action/effect/activate?effect=" + String(i) + "'>";
    html += EFFECT_NAMES[i];
    html += "</button>";
  }
  renderEffectParamsForm(html);
  renderSaveEffectForm(html);
}

static void renderSavedFragment(String& html) {
  uint8_t activeId = getActivePresetId();
  html += "<h1>Saved Effects</h1>";

  uint8_t ids[MAX_EFFECT_PRESETS];
  uint8_t n = presetListIds(ids, MAX_EFFECT_PRESETS);
  if (n == 0) html += "<div class='stub'>No saved effects yet. Configure one on the Effects tab and save it.</div>";

  for (uint8_t i = 0; i < n; i++) {
    const EffectPreset* pr = presetGet(ids[i]);
    if (!pr) continue;
    const Category* cat = (pr->categoryId != CATEGORY_ID_NONE) ? categoryGet(pr->categoryId) : nullptr;

    html += "<div class='row" + String(ids[i] == activeId ? " active" : "") + "'><div>";
    html += "<strong>" + htmlEscape(pr->name) + "</strong><br>";
    html += "<small>" + String(EFFECT_NAMES[pr->effectType]) + (cat ? " &middot; " + htmlEscape(cat->name) : "") + "</small>";
    html += "</div>";
    html += "<button class='tbtn' data-action='/action/preset/activate?id=" + String(ids[i]) + "' data-goto-tab='effects'>Activate</button>";
    html += "<button class='tbtn' data-action='/action/preset/delete?id=" + String(ids[i]) + "' data-confirm='Delete this saved effect?'>Delete</button>";
    html += "</div>";
  }
}

static void renderPalettesFragment(String& html) {
  int editId = server.hasArg("edit") ? server.arg("edit").toInt() : -1;
  const NamedPalette* editing = (editId >= 0 && editId < 255) ? paletteGet((uint8_t)editId) : nullptr;

  html += "<h1>Palettes</h1>";

  uint8_t ids[MAX_PALETTES];
  uint8_t n = paletteListIds(ids, MAX_PALETTES);
  for (uint8_t i = 0; i < n; i++) {
    const NamedPalette* np = paletteGet(ids[i]);
    if (!np) continue;
    html += "<div class='row'><div>";
    html += "<div class='swatches'>";
    for (uint8_t c = 0; c < np->palette.count; c++) {
      html += "<span style='background:" + colorToHex(np->palette.colors[c]) + "'></span>";
    }
    html += "</div>" + htmlEscape(np->name) + "</div>";
    html += "<button class='tbtn' data-edit='/fragment/palettes?edit=" + String(ids[i]) + "'>Edit</button>";
    html += "<button class='tbtn' data-action='/action/palette/delete?id=" + String(ids[i]) + "' data-confirm='Delete this palette?'>Delete</button>";
    html += "</div>";
  }

  html += "<h2>" + String(editing ? "Edit palette" : "New palette") + "</h2>";
  html += "<form data-save='/action/palette/save'>";
  if (editing) html += "<input type='hidden' name='id' value='" + String(editId) + "'>";
  html += "<label>Name <input type='text' name='name' maxlength='" + String(PALETTE_NAME_LEN - 1) + "' value='" + (editing ? htmlEscape(editing->name) : "") + "' required></label>";

  uint8_t count = editing ? editing->palette.count : 3;
  html += "<label>Colors <select name='count'>";
  for (uint8_t c = 1; c <= MAX_PALETTE_COLORS; c++) {
    html += "<option value='" + String(c) + "'" + (c == count ? " selected" : "") + ">" + String(c) + "</option>";
  }
  html += "</select></label>";

  html += "<label>";
  for (uint8_t i = 0; i < MAX_PALETTE_COLORS; i++) {
    CRGB c = (editing && i < editing->palette.count) ? editing->palette.colors[i] : CRGB::Black;
    html += "<input type='color' name='c" + String(i) + "' value='" + colorToHex(c) + "'>";
  }
  html += "</label>";

  html += "<input type='submit' value='" + String(editing ? "Update" : "Create") + "'>";
  html += "</form>";
}

static void renderSettingsFragment(String& html) {
  AudioSettings& s = audioSettings();
  html += "<h1>Settings</h1>";

  html += "<h2>Mic Calibration</h2>";
  html += "<div id='vu'><div id='vuBar'></div></div>";
  html += "<div id='beatDot'></div>";
  html += "<div class='band'><span id='bassBar'></span></div><small>Bass</small>";
  html += "<div class='band'><span id='midBar'></span></div><small>Mid</small>";
  html += "<div class='band'><span id='trebBar'></span></div><small>Treble</small>";
  html += "<div id='bpmReadout'>-- BPM</div><div id='confReadout'>lock: --</div>";
  html += "<div class='tempoRow'>";
  html += "<button class='tbtn' onclick='__nudgeTempo(-1)'>&divide;2</button>";
  html += "<button class='tbtn' onclick='__tapTempo()'>TAP</button>";
  html += "<button class='tbtn' onclick='__nudgeTempo(1)'>&times;2</button>";
  html += "</div>";

  // sliders live-apply on every drag (data-live); the submit button
  // additionally persists the already-live values to flash (data-save) so
  // they survive a reboot
  html += "<form data-live='/action/mic/params' data-save='/action/mic/save'>";
  html += "<label>Sensitivity <input type='range' name='gain' min='1' max='100' value='" + String(s.gain) + "'></label>";
  html += "<label>Noise Floor <input type='range' name='noiseFloor' min='0' max='100' value='" + String(s.noiseFloor) + "'></label>";
  html += "<label>Beat Threshold % <input type='range' name='beatThreshold' min='110' max='400' value='" + String(s.beatThreshold) + "'></label>";
  html += "<label>Beat Debounce (ms) <input type='range' name='beatDebounce' min='30' max='500' value='" + String(s.beatDebounceMs) + "'></label>";
  html += "<input type='submit' value='Apply (save to flash)'>";
  html += "</form>";

  html += "<h2>Mic Recording</h2>";
  html += "<div class='stub' style='padding:0.5em 0;text-align:left'>Records 10 seconds of raw mic "
          "audio to a .wav file, so you can listen back and judge how noisy the mic/preamp is.</div>";
  html += "<button class='tbtn' data-action='/action/mic/record'>Record 10 seconds</button>";
  html += "<div id='micRecordStatus'></div>";
  html += "<a id='micRecordLink' class='link' href='" MIC_RECORDING_PATH "' download style='display:none'>Download recording (.wav)</a>";

  html += "<h2>Firmware Update</h2>";
  html += "<form id='updateForm' method='POST' action='/action/update' enctype='multipart/form-data'>";
  html += "<input type='file' name='firmware' accept='.bin'>";
  html += "<input type='submit' value='Upload'>";
  html += "<div id='updateStatus'></div>";
  html += "</form>";
}

// ---- fragment routes ----

static void handleRoot() {
  server.send(200, "text/html", SHELL_HTML);
}

static void handleFragmentMain() {
  String html;
  renderMainFragment(html);
  server.send(200, "text/html", html);
}

static void handleFragmentEffects() {
  String html;
  renderEffectsFragment(html);
  server.send(200, "text/html", html);
}

static void handleFragmentSaved() {
  String html;
  renderSavedFragment(html);
  server.send(200, "text/html", html);
}

static void handleFragmentPalettes() {
  String html;
  renderPalettesFragment(html);
  server.send(200, "text/html", html);
}

static void handleFragmentSettings() {
  String html;
  renderSettingsFragment(html);
  server.send(200, "text/html", html);
}

// ---- status API (polled by the status bar, and by the Settings tab's meters) ----

static void handleApiStatus() {
  if (server.hasArg("mic")) {
    audioMarkMicPageActive(millis());
  }

  EffectId active = getActiveEffect();
  uint8_t presetId = getActivePresetId();
  const EffectPreset* preset = (presetId != PRESET_ID_NONE) ? presetGet(presetId) : nullptr;
  AudioFeatures f = audioFeatures();

  String json = "{";
  json += "\"effectId\":" + String((int)active);
  json += ",\"effectName\":\"" + jsonEscape(EFFECT_NAMES[active]) + "\"";
  json += ",\"presetName\":" + (preset ? ("\"" + jsonEscape(preset->name) + "\"") : String("null"));
  json += ",\"level\":" + String(f.volume, 3);
  json += ",\"beat\":" + String(audioBeatActive() ? "true" : "false");
  json += ",\"bass\":" + String(f.bass, 3);
  json += ",\"mid\":" + String(f.mid, 3);
  json += ",\"treble\":" + String(f.treble, 3);
  json += ",\"bpm\":" + String((int)(f.bpm + 0.5f));
  json += ",\"conf\":" + String(f.confidence, 2);
  json += ",\"beatSync\":" + String(getBeatSyncEnabled() ? "true" : "false");
  json += ",\"micRecording\":" + String(micRecordInProgress() ? "true" : "false");
  json += ",\"micRecordProgress\":" + String(micRecordProgress(), 2);
  json += ",\"micRecordReady\":" + String(micRecordReady() ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

// ---- main/beat-sync actions ----

static void handleBeatSyncAction() {
  setBeatSyncEnabled(server.hasArg("enabled") ? server.arg("enabled").toInt() != 0 : true);
  server.send(204);
}

// ---- effect actions ----

static void handleEffectActivate() {
  if (server.hasArg("effect")) {
    int id = server.arg("effect").toInt();
    if (id >= 0 && id < EFFECT_COUNT) {
      setActiveEffect((EffectId)id);
    }
  }
  server.send(204);
}

static void handleEffectParamsAction() {
  EffectId active = getActiveEffect();
  if (active != EFFECT_OFF) {
    EffectParams& p = effectParamsFor(active);
    int v;

    if (server.hasArg("paletteId")) {
      String pid = server.arg("paletteId");
      if (pid == "none") {
        setEffectPaletteId(active, PALETTE_ID_NONE);
      } else {
        int id = pid.toInt();
        if (id >= 0 && id < 255 && paletteGet((uint8_t)id) != nullptr) {
          setEffectPaletteId(active, (uint8_t)id);
        }
      }
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
  server.send(204);
}

static void handleEffectSave() {
  EffectId active = getActiveEffect();
  if (active != EFFECT_OFF && server.hasArg("name") && server.arg("name").length() > 0) {
    String name = server.arg("name");

    uint8_t categoryId = CATEGORY_ID_NONE;
    if (server.hasArg("newCategory") && server.arg("newCategory").length() > 0) {
      categoryId = categoryGetOrCreate(server.arg("newCategory").c_str());
    } else if (server.hasArg("categoryId") && server.arg("categoryId").length() > 0) {
      uint8_t cid = (uint8_t)server.arg("categoryId").toInt();
      if (categoryGet(cid) != nullptr) categoryId = cid;
    }

    uint8_t paletteId = getEffectPaletteId(active);
    const EffectParams& params = effectParamsFor(active);

    uint8_t id = PRESET_ID_NONE;
    if (server.hasArg("id")) {
      uint8_t existingId = (uint8_t)server.arg("id").toInt();
      if (presetGet(existingId) != nullptr) {
        presetUpdate(existingId, name.c_str(), active, paletteId, categoryId, params);
        id = existingId;
      }
    }
    if (id == PRESET_ID_NONE) {
      id = presetCreate(name.c_str(), active, paletteId, categoryId, params);
    }
    if (id != PRESET_ID_NONE) setActivePresetId(id);
  }
  server.send(204);
}

// ---- saved-preset actions ----

static void handlePresetActivate() {
  if (server.hasArg("id")) loadEffectPreset((uint8_t)server.arg("id").toInt());
  server.send(204);
}

static void handlePresetDelete() {
  if (server.hasArg("id")) presetDelete((uint8_t)server.arg("id").toInt());
  server.send(204);
}

// ---- palette actions ----

static void handlePaletteSave() {
  if (server.hasArg("name") && server.arg("name").length() > 0) {
    Palette pal;
    int count = server.hasArg("count") ? server.arg("count").toInt() : 3;
    if (count < 1) count = 1;
    if (count > MAX_PALETTE_COLORS) count = MAX_PALETTE_COLORS;
    pal.count = (uint8_t)count;
    for (uint8_t i = 0; i < MAX_PALETTE_COLORS; i++) {
      String argName = "c" + String(i);
      pal.colors[i] = server.hasArg(argName) ? hexToColor(server.arg(argName)) : CRGB::Black;
    }

    String name = server.arg("name");
    if (server.hasArg("id")) {
      paletteUpdate((uint8_t)server.arg("id").toInt(), name.c_str(), pal);
    } else {
      paletteCreate(name.c_str(), pal);
    }
  }
  server.send(204);
}

static void handlePaletteDelete() {
  if (server.hasArg("id")) paletteDelete((uint8_t)server.arg("id").toInt());
  server.send(204);
}

// ---- mic actions ----

static void applyMicParamsFromRequest() {
  AudioSettings& s = audioSettings();
  int v;
  if (argInt("gain", 1, 100, v)) s.gain = v;
  if (argInt("noiseFloor", 0, 100, v)) s.noiseFloor = v;
  if (argInt("beatThreshold", 110, 400, v)) s.beatThreshold = v;
  if (argInt("beatDebounce", 30, 500, v)) s.beatDebounceMs = v;
}

// called live on every slider drag; applies immediately but doesn't touch flash
static void handleMicParamsAction() {
  applyMicParamsFromRequest();
  server.send(204);
}

// called by the Apply button; applies the submitted values and persists them
static void handleMicSaveAction() {
  applyMicParamsFromRequest();
  audioSaveSettings();
  server.send(204);
}

static void handleMicTapAction() {
  audioTap();
  audioMarkMicPageActive(millis());
  server.send(204);
}

static void handleMicNudgeAction() {
  int dir = server.hasArg("dir") ? server.arg("dir").toInt() : 1;
  audioTempoNudge(dir);
  server.send(204);
}

// ---- mic recording ----

static void handleMicRecordAction() {
  micRecordStart();
  server.send(204);
}

static void handleMicRecordingDownload() {
  if (!micRecordReady()) { server.send(404, "text/plain", "No recording available yet"); return; }
  File f = LittleFS.open(MIC_RECORDING_PATH, "r");
  if (!f) { server.send(404, "text/plain", "No recording available yet"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=\"mic_recording.wav\"");
  server.streamFile(f, "audio/wav");
  f.close();
}

// ---- firmware update ----

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
  server.on("/", HTTP_GET, handleRoot);

  server.on("/fragment/main", HTTP_GET, handleFragmentMain);
  server.on("/fragment/effects", HTTP_GET, handleFragmentEffects);
  server.on("/fragment/saved", HTTP_GET, handleFragmentSaved);
  server.on("/fragment/palettes", HTTP_GET, handleFragmentPalettes);
  server.on("/fragment/settings", HTTP_GET, handleFragmentSettings);

  server.on("/api/status", HTTP_GET, handleApiStatus);

  server.on("/action/beatsync", HTTP_POST, handleBeatSyncAction);

  server.on("/action/effect/activate", HTTP_POST, handleEffectActivate);
  server.on("/action/effect/params", HTTP_POST, handleEffectParamsAction);
  server.on("/action/effect/save", HTTP_POST, handleEffectSave);

  server.on("/action/preset/activate", HTTP_POST, handlePresetActivate);
  server.on("/action/preset/delete", HTTP_POST, handlePresetDelete);

  server.on("/action/palette/save", HTTP_POST, handlePaletteSave);
  server.on("/action/palette/delete", HTTP_POST, handlePaletteDelete);

  server.on("/action/mic/params", HTTP_POST, handleMicParamsAction);
  server.on("/action/mic/save", HTTP_POST, handleMicSaveAction);
  server.on("/action/mic/tap", HTTP_POST, handleMicTapAction);
  server.on("/action/mic/nudge", HTTP_POST, handleMicNudgeAction);
  server.on("/action/mic/record", HTTP_POST, handleMicRecordAction);
  server.on(MIC_RECORDING_PATH, HTTP_GET, handleMicRecordingDownload);

  server.on("/action/update", HTTP_POST, handleUpdateResult, handleUpdateUpload);

  server.begin();
}

void webUIHandle() {
  server.handleClient();
}
