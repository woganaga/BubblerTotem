# Bubbler Totem

Firmware for a music-reactive LED totem: 6 stacked rings of 27 WS2811 LEDs
(162 total) driven by an ESP32-S3. The primary control interface is Web
Bluetooth (see below); WiFi/HTTP is a secondary, opt-in interface used
mainly for firmware updates.

This is the PlatformIO project. It was ported from the original Arduino IDE
sketch at `../BubblerIno`, which is no longer the active codebase.

## Hardware

- **MCU:** ESP32-S3 (8MB flash, dual-app OTA partition layout)
- **LEDs:** 6 rings × 27 WS2811 pixels, wired as two chains of 3 rings each
  - Pin 4: rings 3, 2, 1
  - Pin 5: rings 4, 5, 6
  - Physical layout/spacing lives in `Topology.*`; LED brightness is capped
    per-pixel (40%) and per-array (15%) in `Rings.cpp` to keep current draw
    in check.
- **Mic:** INMP441 I2S microphone
  - Pin 44: SCK, Pin 7: WS, Pin 8: SD

## Features

- **Effects** (`EffectManager.*`, `Effects.*`): 12 original effects (Vertical/
  Horizontal Sweep, Alternate Flash, Chase, Spiral, Snow, Pinwheel, Colorwash,
  Fire, Confetti, Ripple) plus 8 effects ported from xLights (`XLFX.*`) that
  render onto the ring array treated as an unrolled 27×6 grid (Bars,
  Colorwash, Spirals, Pinwheel, Butterfly, Plasma, SingleStrand, Morph). Each
  effect keeps its own parameters (palette, speed, intensity, direction, and
  effect-specific controls) across activations.
- **Music reactivity** (`AudioInput.*`): a FreeRTOS task pinned to core 0 runs
  a 512-sample Hann-windowed FFT over the mic input, publishing a lock-free
  `AudioFeatures` snapshot — bass/mid/treble energy, smoothed volume, beat
  detection, and a BPM estimate (autocorrelation of the onset envelope + a
  PLL beat clock) with tap-tempo and ×2/÷2 override.
- **Named palettes & saved effect presets** (`PaletteStore.*`,
  `CategoryStore.*`, `EffectPresetStore.*`): color palettes are created,
  edited, and deleted independently of any one effect, then *linked* to
  whichever effect is showing them (edit a palette and any effect currently
  using it updates live). A full effect configuration (type + palette link +
  params) can be named, categorized, and saved as a preset for one-tap
  recall later. All persisted to LittleFS.
- **Bluetooth control (primary interface)** (`BleServer.*`): a custom BLE
  GATT service (one write characteristic for commands, one notify
  characteristic for chunked JSON responses) exposes the full control
  surface — effects, palettes, presets, categories, mic calibration, WiFi
  on/off — to the standalone Web Bluetooth app at
  [`docs/index.html`](docs/index.html) (meant to be published somewhere
  HTTPS, e.g. GitHub Pages — Web Bluetooth requires a secure context and
  doesn't work from the device's own plain-HTTP page). **Safari on iOS/macOS
  does not support Web Bluetooth at all** (Apple's stated position, not a
  bug) — on iPhone, use a bridge browser like
  [Bluefy](https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055)
  or WebBLE instead; desktop/Android Chrome and Edge work directly.
- **Web UI over WiFi** (`WebUI.*`, `WebApp.*`), a secondary interface served
  on port 80 once WiFi is on — a single-page app with the same Mode/Effects/
  Saved/Palettes/Settings sections as the BLE app, plus a **Firmware Update**
  page under Settings that only shows up here (not over BLE, since pushing a
  multi-hundred-KB binary over BLE characteristics isn't practical).
- **WiFi** (`WifiSetup.*`): station mode, **off by default** — BLE is the
  primary interface, so the radio only comes on when explicitly enabled
  (toggle lives in the BLE app's Settings tab; the on/off choice persists to
  LittleFS across reboots). Hardcoded SSID/password/hostname — edit
  `WifiSetup.cpp` before flashing for a new network.
- **OTA updates** (`OTAUpdate.*`): `ArduinoOTA`, password-protected, an
  alternative to the WiFi web UI's Firmware Update page — both require WiFi
  to be turned on first.

## Building & flashing

Requires [PlatformIO](https://platformio.org/). This project uses the
[pioarduino](https://github.com/pioarduino/platform-espressif32) community
platform instead of the official `espressif32` platform, because it tracks
arduino-esp32 core 3.x — the official platform is still on core 2.x and
lacks the `ESP_I2S.h` API the mic driver depends on.

```sh
pio run                 # build
pio run -t upload       # build + flash over USB
pio run -t uploadfs     # push the LittleFS filesystem (mic settings, etc.)
pio device monitor      # serial monitor
```

Once the device is on the network, subsequent updates can go over OTA
(`espota`) or the WiFi web UI's Firmware Update page instead of USB. WiFi
being off by default means the very first flash (and any time it's been
turned off since) has to be over USB.

## Setting up the Bluetooth app

`docs/index.html` is a standalone static page — it isn't served by the
device. Publish it somewhere HTTPS (Web Bluetooth requires that) and
bookmark it on your phone/laptop:

1. GitHub repo → **Settings → Pages** → Source: "Deploy from a branch" →
   branch `main`, folder `/docs` → Save.
2. GitHub gives you a URL like `https://<user>.github.io/BubblerTotem/`.
   Open it, tap **Connect**, and pick "BubblerTotem" from the Bluetooth
   device list.
3. On iPhone, do this inside Bluefy or WebBLE (not Safari) — see the Web UI
   note above.

## Project layout

```
platformio.ini       build config (board, pioarduino platform, lib_deps)
partitions.csv        8MB flash partition table (dual OTA app slots + LittleFS)
docs/index.html      standalone Web Bluetooth app (publish via GitHub Pages)
src/
  main.cpp            setup()/loop()
  Rings.*              LED buffer, ring addressing, brightness caps
  Topology.*            physical ring spacing/geometry
  EffectManager.*       active effect + per-effect param/palette-link storage/dispatch
  EffectUI.*            per-effect-type UI capability table, shared by WebUI and BleServer
  Effects.*             original effects
  XLFX.*                xLights-derived effects
  Palette.h / Color.h / EffectParams.h   shared color/param types
  PaletteStore.*         named/saved color palettes (LittleFS)
  CategoryStore.*        named categories for saved effects (LittleFS)
  EffectPresetStore.*     named/saved effect configurations (LittleFS)
  AudioInput.*          I2S mic capture, FFT/DSP task, beat/BPM detection
  BleServer.*           Web Bluetooth GATT service + JSON command dispatch (primary interface)
  WebUI.* / WebApp.*     WiFi HTTP server + single-page app (secondary interface)
  WifiSetup.*           WiFi station connect, off by default, persisted on/off toggle
  OTAUpdate.*           ArduinoOTA
```
