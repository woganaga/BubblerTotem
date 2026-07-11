# BubblerTotem

Bubble-LED-Rage-Totem: a battery-powered LED totem / bubble machine — 6
stacked rings of 27 WS2811 LEDs (162 total, an unrolled 27×6 cylinder) plus a
bubble motor, driven by an ESP32-S3 with an INMP441 I2S microphone for
sound-reactive effects.

The firmware is a customized fork of
[WLED-MM (MoonModules)](https://github.com/MoonModules/WLED-MM), chosen for
its advanced audio visualizations.

## Project layout

| Path | Status | What it is |
|---|---|---|
| [`WLED-MM/`](WLED-MM/) | **active** | Firmware: WLED-MM fork (git submodule, branch `bubbler`). Totem customizations live in its `bubbler/` folder + usermods. |
| [`BubblerAnalyzer/`](BubblerAnalyzer/) | active | Offline audio/beat-detection analysis: pulls synchronized mic WAV + DSP metadata captures from the device over WiFi and diffs the pipeline against an offline replica. |
| [`docs/`](docs/) | active (port pending) | Web Bluetooth control app (GitHub Pages). Built for the v1 firmware; will be reworked once WLED-MM gets its BLE usermod. |
| [`archive/Bubbler-pio/`](archive/Bubbler-pio/) | retired | v1 custom firmware (PlatformIO): effects engine, palettes/presets, BLE protocol, beat detection. Superseded by the WLED-MM fork; kept as reference — the beat-detection work (comb tempo picker) and BLE chunk protocol are worth porting. |
| [`archive/BubblerIno/`](archive/BubblerIno/) | retired | Original Arduino IDE sketch (v0). |

## Working with the firmware submodule

```sh
git clone --recurse-submodules https://github.com/woganaga/BubblerTotem.git
cd BubblerTotem/WLED-MM
# copy bubbler/platformio_bubbler.sample.ini -> platformio_override.ini, add WiFi creds
pio run -e bubbler_totem
```

The submodule's `origin` is the `woganaga/WLED-MM` fork; `upstream` is
MoonModules. Custom work stays on the `bubbler` branch in low-collision
surfaces (`bubbler/`, usermods, the gitignored `platformio_override.ini`) so
upstream merges stay painless.

## Hardware map

| Function | GPIO |
|---|---|
| LED chain A (rings 3,2,1 — 81 px, WS2811 GRB) | 4 |
| LED chain B (rings 4,5,6 — 81 px) | 5 |
| INMP441 mic SCK / WS / SD | 44 / 7 / 8 |
| Bubble motor (planned) | 15 |
| RS485 command receiver (planned) | 1, 2, 42 |
| Onboard status LED (active low) | 21 |
