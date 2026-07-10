#include "BleServer.h"
#include <NimBLEDevice.h>
#include <string.h>
#include "EffectManager.h"
#include "EffectUI.h"
#include "PaletteStore.h"
#include "CategoryStore.h"
#include "EffectPresetStore.h"
#include "AudioInput.h"
#include "WifiSetup.h"
#include "WebCommon.h"

#define BLE_DEVICE_NAME "BubblerTotem"

// Custom 128-bit UUIDs (not a standardized service - just distinct/unlikely
// to collide). Must match docs/index.html exactly.
static const char* SERVICE_UUID = "7a5a1000-0002-4b70-8f1a-9d6e9c9a2b10";
static const char* RX_CHAR_UUID = "7a5a1001-0002-4b70-8f1a-9d6e9c9a2b10"; // write: commands in
static const char* TX_CHAR_UUID = "7a5a1002-0002-4b70-8f1a-9d6e9c9a2b10"; // notify: JSON responses out

// Responses can be a few KB (e.g. "meta"), well beyond one ATT MTU, so they
// go out as a sequence of GATT indications (not notifications - indications
// require the peer to ack each one before the next can be sent, which is
// what makes multi-chunk delivery *at the radio layer* reliable; plain
// notifications have no flow control and a chunk can silently drop). Each
// chunk is prefixed with three bytes: [0]=more (1 if more chunks follow, 0
// if last), [1]=request id (echoed from the command's "_id" param), [2]=
// chunk sequence number within this response (wraps at 256). Two earlier,
// weaker designs are why this has both a request id AND a sequence number:
// GATT-level acking only proves the phone's Bluetooth stack got a chunk, not
// that the phone's *app* (e.g. Bluefy's bridge into its WebView) relayed it
// to the page intact - a sequence number alone catches a gap/repeat/reorder
// *within* one response, but a late straggler chunk from a *previous*
// request (a retry, a fast poll) can still land during a new one and
// coincidentally match its expected sequence number. The request id lets the
// client tell "stale chunk from an old request" (silently ignored) apart
// from "corrupted chunk in the current request" (rejected with a clear
// error) instead of silently concatenating either into bad JSON.
//
// 19 data bytes (+ 3 header bytes = 22) is deliberately conservative: it's
// close to the largest payload that reliably fits in an ATT PDU even at the
// default, un-negotiated minimum MTU (23 bytes total / 20 usable), so this
// can't be truncated regardless of what MTU the central actually negotiates
// or when. Once this is confirmed reliable on real hardware, this can
// likely be raised (e.g. ~180) for fewer round trips.
static const size_t BLE_CHUNK_SIZE = 19;

static NimBLECharacteristic* txChar = nullptr;

// ---- tiny key=value command parser (percent-decoded), mirroring
// WebServer's hasArg()/arg() but over the BLE command string ----
class BleParams {
 public:
  explicit BleParams(const String& raw) { parse(raw); }

  bool has(const char* name) const { return find(name) != nullptr; }

  String get(const char* name, const String& def = "") const {
    const String* v = find(name);
    return v ? *v : def;
  }

  int getInt(const char* name, int def) const {
    const String* v = find(name);
    return v ? v->toInt() : def;
  }

 private:
  struct KV { String key; String value; };
  static const uint8_t MAX_ITEMS = 24;
  KV items[MAX_ITEMS];
  uint8_t count = 0;

  static String urlDecode(const String& s) {
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
      char c = s[i];
      if (c == '%' && i + 2 < s.length()) {
        char hex[3] = { s[i + 1], s[i + 2], 0 };
        out += (char)strtol(hex, nullptr, 16);
        i += 2;
      } else {
        out += c;
      }
    }
    return out;
  }

  void parse(const String& raw) {
    int start = 0;
    while (start < (int)raw.length() && count < MAX_ITEMS) {
      int amp = raw.indexOf('&', start);
      String pair = (amp < 0) ? raw.substring(start) : raw.substring(start, amp);
      int eq = pair.indexOf('=');
      if (eq >= 0) {
        items[count].key = pair.substring(0, eq);
        items[count].value = urlDecode(pair.substring(eq + 1));
        count++;
      }
      if (amp < 0) break;
      start = amp + 1;
    }
  }

  const String* find(const char* name) const {
    for (uint8_t i = 0; i < count; i++) if (items[i].key == name) return &items[i].value;
    return nullptr;
  }
};

// mirrors WebUI.cpp's argInt, operating on BleParams instead of WebServer
static bool argInt(const BleParams& p, const char* name, int lo, int hi, int& out) {
  if (!p.has(name)) return false;
  int v = p.getInt(name, lo);
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  out = v;
  return true;
}

static void sendChunked(const String& payload, uint8_t reqId) {
  if (!txChar) return;
  size_t len = payload.length();
  size_t offset = 0;
  uint8_t buf[BLE_CHUNK_SIZE + 3];
  uint8_t seq = 0;
  uint32_t startMs = millis();

  Serial.printf("BLE TX: reqId=%u %u bytes total, chunk size %u -> ~%u chunks\n",
    reqId, (unsigned)len, (unsigned)BLE_CHUNK_SIZE, (unsigned)((len + BLE_CHUNK_SIZE - 1) / BLE_CHUNK_SIZE));

  do {
    size_t remaining = len - offset;
    size_t chunkLen = remaining > BLE_CHUNK_SIZE ? BLE_CHUNK_SIZE : remaining;
    bool more = (offset + chunkLen) < len;
    buf[0] = more ? 1 : 0;
    buf[1] = reqId;
    buf[2] = seq;
    memcpy(buf + 3, payload.c_str() + offset, chunkLen);
    txChar->setValue(buf, chunkLen + 3);
    bool acked = txChar->indicate();
    Serial.printf("BLE TX: reqId=%u chunk seq=%u offset=%u len=%u more=%d acked=%d\n",
      reqId, seq, (unsigned)offset, (unsigned)chunkLen, more ? 1 : 0, acked ? 1 : 0);
    if (!acked) {
      Serial.println("BLE TX: indicate() failed (peer didn't ack - disconnected/timed out?), aborting response");
      return; // stop rather than send more into the void; response is incomplete on the client
    }
    offset += chunkLen;
    seq++;
  } while (offset < len);

  if (len == 0) {
    uint8_t buf0[3] = { 0, reqId, 0 };
    txChar->setValue(buf0, 3);
    txChar->indicate();
  }
  Serial.printf("BLE TX: reqId=%u done, %u chunks in %lums\n", reqId, seq, (unsigned long)(millis() - startMs));
}

// ---- JSON builders (mirror the shapes WebUI.cpp already sends over HTTP) ----

static String jsonEffectParams(EffectId id) {
  EffectParams& p = effectParamsFor(id);
  uint8_t paletteId = getEffectPaletteId(id);

  String j = "{";
  j += "\"effectId\":" + String((int)id);
  j += ",\"effectName\":\"" + jsonEscape(EFFECT_NAMES[id]) + "\"";
  j += ",\"paletteId\":" + String(paletteId == PALETTE_ID_NONE ? -1 : (int)paletteId);
  j += ",\"speed\":" + String(p.speedPct);
  j += ",\"intensity\":" + String(p.intensity);
  j += ",\"width\":" + String(p.width);
  j += ",\"direction\":" + String((int)p.direction);
  j += ",\"twists\":" + String(p.twists);
  j += ",\"rows\":" + String(p.rows);
  j += ",\"overlap\":" + String(p.overlap);
  j += ",\"dualChase\":" + String((int)p.dualChase);
  j += ",\"count\":" + String(p.count);
  j += ",\"style\":" + String(p.style);
  j += ",\"density\":" + String(p.density);
  j += ",\"thickness\":" + String(p.thickness);
  j += ",\"twistDeg\":" + String(p.twistDeg);
  j += ",\"flag\":" + String((int)p.flag);
  j += ",\"flag2\":" + String((int)p.flag2);
  j += ",\"paletteColors\":[";
  for (uint8_t i = 0; i < p.palette.count; i++) {
    if (i) j += ",";
    j += "\"" + colorToHex(p.palette.colors[i]) + "\"";
  }
  j += "]}";
  return j;
}

static String jsonStatus() {
  EffectId active = getActiveEffect();
  uint8_t presetId = getActivePresetId();
  const EffectPreset* preset = (presetId != PRESET_ID_NONE) ? presetGet(presetId) : nullptr;
  AudioFeatures f = audioFeatures();

  String j = "{";
  j += "\"effectId\":" + String((int)active);
  j += ",\"effectName\":\"" + jsonEscape(EFFECT_NAMES[active]) + "\"";
  j += ",\"presetId\":" + String(presetId == PRESET_ID_NONE ? -1 : (int)presetId);
  j += ",\"presetName\":" + (preset ? ("\"" + jsonEscape(preset->name) + "\"") : String("null"));
  j += ",\"level\":" + String(f.volume, 3);
  j += ",\"beat\":" + String(audioBeatActive() ? "true" : "false");
  j += ",\"bass\":" + String(f.bass, 3);
  j += ",\"mid\":" + String(f.mid, 3);
  j += ",\"treble\":" + String(f.treble, 3);
  j += ",\"bpm\":" + String((int)(f.bpm + 0.5f));
  j += ",\"conf\":" + String(f.confidence, 2);
  j += ",\"wifiEnabled\":" + String(wifiIsEnabled() ? "true" : "false");
  j += ",\"wifiConnected\":" + String(wifiIsConnected() ? "true" : "false");
  j += "}";
  return j;
}

// per-effect-type UI capability metadata, fetched once so the app can
// render the right controls for whichever effect gets activated
static String jsonMeta() {
  String j = "{\"effects\":[";
  for (uint8_t i = 0; i < EFFECT_COUNT; i++) {
    if (i) j += ",";
    const EffectUIInfo& info = EFFECT_UI[i];
    j += "{\"id\":" + String(i) + ",\"name\":\"" + jsonEscape(EFFECT_NAMES[i]) + "\"";
    if (info.usesWidth) j += ",\"width\":true";
    if (info.usesTwists) j += ",\"twists\":true";
    if (info.usesRows) j += ",\"rows\":true";
    if (info.usesOverlap) j += ",\"overlap\":true";
    if (info.usesDualChase) j += ",\"dualChase\":true";
    if (info.dirOptions) {
      j += ",\"dir\":[";
      for (uint8_t d = 0; d < info.dirCount; d++) {
        if (d) j += ",";
        j += "[\"" + jsonEscape(info.dirOptions[d].label) + "\"," + String((int)info.dirOptions[d].value) + "]";
      }
      j += "]";
    }
    if (info.countLabel) j += ",\"count\":{\"label\":\"" + jsonEscape(info.countLabel) + "\",\"max\":" + String(info.countMax) + "}";
    if (info.styleLabel) j += ",\"style\":{\"label\":\"" + jsonEscape(info.styleLabel) + "\",\"min\":" + String(info.styleMin) + ",\"max\":" + String(info.styleMax) + "}";
    if (info.densityLabel) j += ",\"density\":{\"label\":\"" + jsonEscape(info.densityLabel) + "\",\"max\":" + String(info.densityMax) + "}";
    if (info.thicknessLabel) j += ",\"thickness\":\"" + jsonEscape(info.thicknessLabel) + "\"";
    if (info.twistLabel) j += ",\"twist\":\"" + jsonEscape(info.twistLabel) + "\"";
    if (info.flagLabel) j += ",\"flag\":\"" + jsonEscape(info.flagLabel) + "\"";
    if (info.flag2Label) j += ",\"flag2\":\"" + jsonEscape(info.flag2Label) + "\"";
    j += "}";
  }
  j += "]}";
  return j;
}

static String jsonPresetList() {
  uint8_t ids[MAX_EFFECT_PRESETS];
  uint8_t n = presetListIds(ids, MAX_EFFECT_PRESETS);
  String j = "{\"presets\":[";
  for (uint8_t i = 0; i < n; i++) {
    const EffectPreset* pr = presetGet(ids[i]);
    if (!pr) continue;
    const Category* cat = (pr->categoryId != CATEGORY_ID_NONE) ? categoryGet(pr->categoryId) : nullptr;
    if (i) j += ",";
    j += "{\"id\":" + String(ids[i]) + ",\"name\":\"" + jsonEscape(pr->name) + "\"";
    j += ",\"effectId\":" + String((int)pr->effectType) + ",\"effectName\":\"" + jsonEscape(EFFECT_NAMES[pr->effectType]) + "\"";
    j += ",\"categoryId\":" + String(pr->categoryId == CATEGORY_ID_NONE ? -1 : (int)pr->categoryId);
    j += ",\"categoryName\":" + (cat ? ("\"" + jsonEscape(cat->name) + "\"") : String("null"));
    j += "}";
  }
  j += "]}";
  return j;
}

static String jsonPaletteList() {
  uint8_t ids[MAX_PALETTES];
  uint8_t n = paletteListIds(ids, MAX_PALETTES);
  String j = "{\"palettes\":[";
  for (uint8_t i = 0; i < n; i++) {
    const NamedPalette* np = paletteGet(ids[i]);
    if (!np) continue;
    if (i) j += ",";
    j += "{\"id\":" + String(ids[i]) + ",\"name\":\"" + jsonEscape(np->name) + "\",\"count\":" + String(np->palette.count) + ",\"colors\":[";
    for (uint8_t c = 0; c < np->palette.count; c++) {
      if (c) j += ",";
      j += "\"" + colorToHex(np->palette.colors[c]) + "\"";
    }
    j += "]}";
  }
  j += "]}";
  return j;
}

static String jsonCategoryList() {
  uint8_t ids[MAX_CATEGORIES];
  uint8_t n = categoryListIds(ids, MAX_CATEGORIES);
  String j = "{\"categories\":[";
  for (uint8_t i = 0; i < n; i++) {
    const Category* c = categoryGet(ids[i]);
    if (!c) continue;
    if (i) j += ",";
    j += "{\"id\":" + String(ids[i]) + ",\"name\":\"" + jsonEscape(c->name) + "\"}";
  }
  j += "]}";
  return j;
}

static String jsonMicSettings() {
  AudioSettings& s = audioSettings();
  String j = "{";
  j += "\"gain\":" + String(s.gain);
  j += ",\"noiseFloor\":" + String(s.noiseFloor);
  j += ",\"beatThreshold\":" + String(s.beatThreshold);
  j += ",\"beatDebounce\":" + String(s.beatDebounceMs);
  j += "}";
  return j;
}

static String jsonWifi() {
  String j = "{\"enabled\":" + String(wifiIsEnabled() ? "true" : "false");
  j += ",\"connected\":" + String(wifiIsConnected() ? "true" : "false");
  String ip = wifiLocalIP();
  j += ",\"ip\":" + (ip.length() ? ("\"" + ip + "\"") : String("null"));
  j += "}";
  return j;
}

static String jsonOk() { return "{\"ok\":true}"; }
static String jsonErr(const char* msg) { return String("{\"ok\":false,\"error\":\"") + jsonEscape(msg) + "\"}"; }

// mirrors WebUI.cpp's applyMicParamsFromRequest
static void applyMicParams(const BleParams& p) {
  AudioSettings& s = audioSettings();
  int v;
  if (argInt(p, "gain", 1, 100, v)) s.gain = v;
  if (argInt(p, "noiseFloor", 0, 100, v)) s.noiseFloor = v;
  if (argInt(p, "beatThreshold", 110, 400, v)) s.beatThreshold = v;
  if (argInt(p, "beatDebounce", 30, 500, v)) s.beatDebounceMs = v;
}

// ---- command dispatch: same operations as WebUI.cpp's /action/* routes,
// over a "op=name&field=value&..." command string instead of HTTP verbs/paths ----
static String handleCommand(const String& raw) {
  BleParams p(raw);
  String op = p.get("op");

  if (op == "status") return jsonStatus();
  if (op == "meta") return jsonMeta();
  if (op == "effectParams") return jsonEffectParams(getActiveEffect());

  if (op == "activateEffect") {
    int id = p.getInt("effect", -1);
    if (id < 0 || id >= EFFECT_COUNT) return jsonErr("bad effect id");
    setActiveEffect((EffectId)id);
    return jsonEffectParams(getActiveEffect());
  }

  if (op == "setEffectParams") {
    EffectId active = getActiveEffect();
    if (active == EFFECT_OFF) return jsonErr("no active effect");
    EffectParams& params = effectParamsFor(active);
    int v;

    if (p.has("paletteId")) {
      String pid = p.get("paletteId");
      if (pid == "none" || pid == "-1") {
        setEffectPaletteId(active, PALETTE_ID_NONE);
      } else {
        int id = pid.toInt();
        if (id >= 0 && id < 255 && paletteGet((uint8_t)id) != nullptr) setEffectPaletteId(active, (uint8_t)id);
      }
    }
    if (argInt(p, "speed", 1, 100, v)) params.speedPct = v;
    if (argInt(p, "intensity", 1, 100, v)) params.intensity = v;
    if (argInt(p, "width", 1, 3, v)) params.width = v;
    if (p.has("direction")) params.direction = (Direction)p.getInt("direction", (int)params.direction);
    if (argInt(p, "twists", 1, 10, v)) params.twists = v;
    if (argInt(p, "rows", 1, 6, v)) params.rows = v;
    if (argInt(p, "overlap", 1, 100, v)) params.overlap = v;
    if (p.has("dualChase")) params.dualChase = p.getInt("dualChase", 0) ? 1 : 0;
    if (argInt(p, "elemcount", 1, 12, v)) params.count = v;
    if (argInt(p, "style", 0, 9, v)) params.style = v;
    if (argInt(p, "density", 1, 32, v)) params.density = v;
    if (argInt(p, "thickness", 0, 100, v)) params.thickness = v;
    if (argInt(p, "twist", -360, 360, v)) params.twistDeg = v;
    if (p.has("flag")) params.flag = p.getInt("flag", 0) ? 1 : 0;
    if (p.has("flag2")) params.flag2 = p.getInt("flag2", 0) ? 1 : 0;
    return jsonOk();
  }

  if (op == "saveEffect") {
    EffectId active = getActiveEffect();
    String name = p.get("name");
    if (active == EFFECT_OFF || name.length() == 0) return jsonErr("no active effect or missing name");

    uint8_t categoryId = CATEGORY_ID_NONE;
    if (p.has("newCategory") && p.get("newCategory").length() > 0) {
      categoryId = categoryGetOrCreate(p.get("newCategory").c_str());
    } else if (p.has("categoryId") && p.get("categoryId").length() > 0) {
      uint8_t cid = (uint8_t)p.getInt("categoryId", -1);
      if (categoryGet(cid) != nullptr) categoryId = cid;
    }

    uint8_t paletteId = getEffectPaletteId(active);
    const EffectParams& params = effectParamsFor(active);

    uint8_t id = PRESET_ID_NONE;
    if (p.has("id")) {
      uint8_t existingId = (uint8_t)p.getInt("id", -1);
      if (presetGet(existingId) != nullptr) {
        presetUpdate(existingId, name.c_str(), active, paletteId, categoryId, params);
        id = existingId;
      }
    }
    if (id == PRESET_ID_NONE) id = presetCreate(name.c_str(), active, paletteId, categoryId, params);
    if (id == PRESET_ID_NONE) return jsonErr("preset store full");
    setActivePresetId(id);
    return "{\"ok\":true,\"id\":" + String(id) + "}";
  }

  if (op == "listPresets") return jsonPresetList();

  if (op == "activatePreset") {
    int id = p.getInt("id", -1);
    if (id < 0 || !loadEffectPreset((uint8_t)id)) return jsonErr("bad preset id");
    return jsonEffectParams(getActiveEffect());
  }

  if (op == "deletePreset") {
    presetDelete((uint8_t)p.getInt("id", -1));
    return jsonOk();
  }

  if (op == "listPalettes") return jsonPaletteList();

  if (op == "savePalette") {
    String name = p.get("name");
    if (name.length() == 0) return jsonErr("missing name");
    Palette pal;
    int count = p.getInt("count", 3);
    if (count < 1) count = 1;
    if (count > MAX_PALETTE_COLORS) count = MAX_PALETTE_COLORS;
    pal.count = (uint8_t)count;
    for (uint8_t i = 0; i < MAX_PALETTE_COLORS; i++) {
      String key = "c" + String(i);
      pal.colors[i] = p.has(key.c_str()) ? hexToColor(p.get(key.c_str())) : CRGB::Black;
    }
    uint8_t id;
    if (p.has("id")) {
      id = (uint8_t)p.getInt("id", -1);
      paletteUpdate(id, name.c_str(), pal);
    } else {
      id = paletteCreate(name.c_str(), pal);
    }
    if (id == PALETTE_ID_NONE) return jsonErr("palette store full");
    return "{\"ok\":true,\"id\":" + String(id) + "}";
  }

  if (op == "deletePalette") {
    paletteDelete((uint8_t)p.getInt("id", -1));
    return jsonOk();
  }

  if (op == "listCategories") return jsonCategoryList();

  if (op == "micSettings") return jsonMicSettings();

  if (op == "setMicParams") {
    applyMicParams(p);
    return jsonOk();
  }

  if (op == "saveMicSettings") {
    applyMicParams(p);
    audioSaveSettings();
    return jsonOk();
  }

  if (op == "micTap") {
    audioTap();
    audioMarkMicPageActive(millis());
    return jsonOk();
  }

  if (op == "micNudge") {
    audioTempoNudge(p.getInt("dir", 1));
    return jsonOk();
  }

  if (op == "getWifi") return jsonWifi();

  if (op == "setWifi") {
    wifiSetEnabled(p.getInt("enabled", 0) != 0);
    return jsonWifi();
  }

  return jsonErr("unknown op");
}

// ---- GATT plumbing ----

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    String cmd(c->getValue().c_str());
    BleParams p(cmd);
    uint8_t reqId = (uint8_t)p.getInt("_id", 0); // client-generated, echoed back in every response chunk
    Serial.printf("BLE RX: reqId=%u %u bytes: %s\n", reqId, (unsigned)cmd.length(), cmd.c_str());
    String response = handleCommand(cmd);
    Serial.printf("BLE: reqId=%u response ready, %u bytes: %s%s\n", reqId, (unsigned)response.length(),
      response.substring(0, 120).c_str(), response.length() > 120 ? "..." : "");
    sendChunked(response, reqId);
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& connInfo) override {
    Serial.printf("BLE: central connected, handle=%u\n", connInfo.getConnHandle());
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& connInfo, int reason) override {
    Serial.printf("BLE: central disconnected, reason=%d - resuming advertising\n", reason);
    NimBLEDevice::startAdvertising(); // resume advertising so another central can connect
  }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo& connInfo) override {
    Serial.printf("BLE: MTU negotiated = %u (usable payload ~%u bytes)\n", mtu, mtu > 3 ? mtu - 3 : 0);
  }
};

void bleServerInit() {
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setMTU(517);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  NimBLEService* service = server->createService(SERVICE_UUID);
  txChar = service->createCharacteristic(TX_CHAR_UUID, NIMBLE_PROPERTY::INDICATE | NIMBLE_PROPERTY::READ);
  NimBLECharacteristic* rxChar = service->createCharacteristic(RX_CHAR_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rxChar->setCallbacks(new RxCallbacks());
  server->start(); // starts all of this server's services (service->start() is deprecated/a no-op now)

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->start();

  Serial.printf("BLE: advertising as \"%s\", service %s\n", BLE_DEVICE_NAME, SERVICE_UUID);
}
