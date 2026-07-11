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
- **Status LED:** GPIO 21 (active low) — blinks in real time with the PLL
  beat clock once a tempo is tracked (raw onsets before that), so the beat
  tracker can be eyeballed against the music with no UI polling latency

## Features

- **Effects** (`EffectManager.*`, `Effects.*`): 12 original effects (Vertical/
  Horizontal Sweep, Alternate Flash, Chase, Spiral, Snow, Pinwheel, Colorwash,
  Fire, Confetti, Ripple) plus a **Beat Flash** test effect (whole array
  flashes and decays once per period — a metronome for the eyes; run it with
  Beat Sync at 1 beat/cycle to see exactly what the tempo tracker is doing)
  plus 8 effects ported from xLights (`XLFX.*`) that
  render onto the ring array treated as an unrolled 27×6 grid (Bars,
  Colorwash, Spirals, Pinwheel, Butterfly, Plasma, SingleStrand, Morph). Each
  effect keeps its own parameters (palette, speed, intensity, direction, and
  effect-specific controls) across activations.
- **Music reactivity** (`AudioInput.*`): a FreeRTOS task pinned to core 0 runs
  a 512-sample Hann-windowed FFT over the mic input, publishing a lock-free
  `AudioFeatures` snapshot — bass/mid/treble energy, smoothed volume, beat
  detection, and a BPM estimate (autocorrelation of the onset envelope + a
  PLL beat clock) with tap-tempo and ×2/÷2 override. **Beat Sync** (toggle on
  the Main tab) tempo-locks the running effect: once the tempo estimator is
  confidently locked, `EffectManager` stops feeding the effect real elapsed
  time and synthesizes its time input from the PLL beat clock, scaled so one
  full effect cycle spans exactly N beats (N auto-picked from ½/1/2/4/8 to
  best match the effect's speed setting, or fixed via the Beats-per-cycle
  buttons) with cycle boundaries landing on beats. The animation speeds up,
  slows down, and stays in phase with the music indefinitely — no per-effect
  changes needed, since each periodic effect is a pure function of its time
  argument and exposes its natural cycle length via
  `effectNativePeriodMs()`/`xlfxNativePeriodMs()`. Stochastic effects with no
  fixed loop (Snow, Fire, Confetti, Ripple, XL Butterfly, XL Plasma)
  free-run; while no confident tempo is detected, everything free-runs (with
  hysteresis so borderline confidence doesn't flap between modes). The Main
  tab shows a live readout of the lock state, and the status bar visualizes
  the tracker at a glance: detected-onset dot, a pulse dot that beats at the
  detected BPM (amber while unlocked, green when sync is locked — re-phased
  to the device's beat clock on every status poll), BPM + lock confidence,
  and a ♪×N badge showing the locked beats-per-cycle. A **Mic Recording** button
  (Settings tab, both transports) captures
  a fixed 10 seconds of raw mic audio to a `.wav` file on LittleFS, so you can
  judge how noisy the mic/preamp actually is by listening back. Triggering
  the recording works over BLE or WiFi, but the finished file (~320KB) is
  only downloadable over WiFi/HTTP — pushing that much data through the BLE
  chunk protocol above would be impractically slow.
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
  [`../docs/index.html`](../docs/index.html) (meant to be published somewhere
  HTTPS, e.g. GitHub Pages — Web Bluetooth requires a secure context and
  doesn't work from the device's own plain-HTTP page). **Safari on iOS/macOS
  does not support Web Bluetooth at all** (Apple's stated position, not a
  bug) — on iPhone, use a bridge browser like
  [Bluefy](https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055)
  or WebBLE instead; desktop/Android Chrome and Edge work directly.
- **Web UI over WiFi** (`WebUI.*`, `WebApp.*`), a secondary interface served
  on port 80 once WiFi is on — a single-page app with the same Main/Effects/
  Saved/Palettes/Settings sections as the BLE app, plus a **Firmware Update**
  page under Settings that only shows up here (not over BLE, since pushing a
  multi-hundred-KB binary over BLE characteristics isn't practical).
- **WiFi** (`WifiSetup.*`): station mode, **off by default** — BLE is the
  primary interface, so the radio only comes on when explicitly enabled
  (toggle lives in the BLE app's Settings tab; the on/off choice persists to
  LittleFS across reboots). SSID/password live in `src/Secrets.h` (gitignored
  — copy `src/Secrets.h.example` to `src/Secrets.h` and fill in your network
  before building).
- **OTA updates** (`OTAUpdate.*`): `ArduinoOTA`, password-protected, an
  alternative to the WiFi web UI's Firmware Update page — both require WiFi
  to be turned on first.

## Building & flashing

Requires [PlatformIO](https://platformio.org/). This project uses the
[pioarduino](https://github.com/pioarduino/platform-espressif32) community
platform instead of the official `espressif32` platform, because it tracks
arduino-esp32 core 3.x — the official platform is still on core 2.x and
lacks the `ESP_I2S.h` API the mic driver depends on.

Before the first build, copy `src/Secrets.h.example` to `src/Secrets.h` and
fill in your WiFi SSID/password (that file is gitignored, so it won't be
committed).

```sh
pio run                 # build (default env: esp32-s3-devkitc-1-debug)
pio run -t upload       # build + flash over USB
pio run -t uploadfs     # push the LittleFS filesystem (mic settings, etc.)
pio device monitor      # serial monitor
```

There are three environments (`platformio.ini`):
- **`esp32-s3-devkitc-1-debug`** — the current default (`pio run` with no
  `-e` builds this). Same as the plain env, plus verbose BLE wire-protocol
  tracing (every chunk/ack/command) to the Serial monitor, gated behind
  `BUBBLER_BLE_DEBUG` (see `BLE_LOG` in `BleServer.cpp`).
- **`esp32-s3-devkitc-1`** — the quiet build, no BLE tracing. Once BLE is
  solid, build this explicitly (`pio run -e esp32-s3-devkitc-1 -t upload`),
  or flip `default_envs` in `platformio.ini` back to it.
- **`esp32-s3-devkitc-1-ota`** — OTA upload variant (extends the plain env).

Once the device is on the network, subsequent updates can go over OTA
(`espota`) or the WiFi web UI's Firmware Update page instead of USB. WiFi
being off by default means the very first flash (and any time it's been
turned off since) has to be over USB.

## Setting up the Bluetooth app

`../docs/index.html` (at the repo root, a sibling of this `Bubbler-pio`
folder — GitHub Pages' "/docs" option is relative to the repo root, not any
subfolder) is a standalone static page — it isn't served by the device.
Publish it somewhere HTTPS (Web Bluetooth requires that) and bookmark it on
your phone/laptop:

1. GitHub repo → **Settings → Pages** → Source: "Deploy from a branch" →
   branch `main`, folder `/docs` → Save.
2. GitHub gives you a URL like `https://<user>.github.io/BubblerTotem/`.
   Open it, tap **Connect**, and pick "BubblerTotem" from the Bluetooth
   device list.
3. On iPhone, do this inside Bluefy or WebBLE (not Safari) — see the Web UI
   note above.

## Project layout

```
../docs/index.html   standalone Web Bluetooth app, at the repo root (publish via GitHub Pages)
platformio.ini       build config (board, pioarduino platform, lib_deps)
partitions.csv        8MB flash partition table (dual OTA app slots + LittleFS)
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
