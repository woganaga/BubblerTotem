#pragma once

// Web Bluetooth control API: a custom GATT service with one write
// characteristic (commands in) and one notify characteristic (JSON
// responses out, chunked - see BleServer.cpp for the wire format). This is
// the primary control surface for the totem; the external Web Bluetooth
// app (docs/index.html, meant to be hosted somewhere HTTPS like GitHub
// Pages) talks to it directly. It reuses the exact same business logic
// (EffectManager, PaletteStore, CategoryStore, EffectPresetStore,
// AudioInput, WifiSetup) as the on-device WiFi WebUI - this is purely an
// additional transport, not a second implementation of the app's behavior.
void bleServerInit();

// call every loop() iteration: actually sends the next response chunk, if
// one is due (see BleServer.cpp - chunk sends are deferred out of the BLE
// callback context and picked up here instead)
void bleServerHandle();
