# Bubbler Totem

Firmware for a music-reactive LED totem: 6 stacked rings of 27 WS2811 LEDs
(162 total) driven by an ESP32-S3, controllable over WiFi from any browser.

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
- **Web UI** (`WebUI.*`), served on port 80:
  - `/` — pick the active effect and edit its live parameters
  - `/mic` — live VU/band meters, BPM readout, mic sensitivity/noise-floor/
    beat-threshold tuning (persisted to LittleFS)
  - `/update` — upload a new firmware `.bin` over HTTP
- **OTA updates** (`OTAUpdate.*`): `ArduinoOTA`, password-protected, as an
  alternative to the web `/update` page.
- **WiFi** (`WifiSetup.*`): station mode, hardcoded SSID/password/hostname —
  edit `WifiSetup.cpp` before flashing for a new network.

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
(`espota`) or the `/update` web page instead of USB.

## Project layout

```
platformio.ini       build config (board, pioarduino platform, lib_deps)
partitions.csv        8MB flash partition table (dual OTA app slots + LittleFS)
src/
  main.cpp            setup()/loop()
  Rings.*              LED buffer, ring addressing, brightness caps
  Topology.*            physical ring spacing/geometry
  EffectManager.*       active effect + per-effect param storage/dispatch
  Effects.*             original effects
  XLFX.*                xLights-derived effects
  Palette.h / Color.h / EffectParams.h   shared color/param types
  AudioInput.*          I2S mic capture, FFT/DSP task, beat/BPM detection
  WebUI.*               HTTP server, control pages, firmware upload
  WifiSetup.*           WiFi station connect
  OTAUpdate.*           ArduinoOTA
```
