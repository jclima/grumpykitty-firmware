<!-- SPDX-License-Identifier: MIT; Copyright (c) 2026 JC Lima -->
# Development

## Prerequisites

- PlatformIO Core (`pio`), on PATH or at `~/.platformio/penv/bin/pio`. Run `make bootstrap-check` to verify.
- `make` and Python 3 (used by `tools/embed_gif.py`, a pre-build script).
- USB data cable to the board's native USB port (ESP32-S3 N16R8).

## Build

```
make build      # pio run -e esp32s3box (first build takes a few minutes)
make size       # memory usage
make clean
```

Generated asset files are build products. `tools/embed_gif.py` regenerates them on every build with machine-specific `.incbin` paths, so they are git-ignored and untracked. Do not commit them:

- `src/splash/splash_gif_data.S`, `src/splash/splash_gif.h`
- `src/system/system_sound_assets.S`, `src/system/system_sound_assets.cpp`
- `src/app/app_03/vu_meter_assets.S`, `src/app/app_03/vu_meter_assets.h`

## Enter Download Mode

The native USB port only flashes reliably from Download Mode:

1. Insert a jumper through the hole on the back to hold the BOOT button down.
2. While holding BOOT, hold Power for 1-2 s.
3. Release Power, then release BOOT. The screen stays off.
4. The board enumerates as a "USB JTAG/serial" device. Check with `make devices`.

## Flash

```
make upload                         # auto-detects the port
make upload PORT=/dev/cu.usbmodemXXXX
make flash-monitor                  # upload, then open the monitor
```

`platformio.ini` uses `upload_protocol = esptool` with no hardcoded port; the Makefile auto-detects one (override with `PORT=`).

**Close the serial monitor before flashing**: it holds the port.

After flashing, reset the board: hold Power 4-5 s to power off, then power on again (or short RESET to GND).

If flashing from source is not possible, use the web installer: https://meowkit.cc/pages/download

> **Warning:** flashing with a full chip erase wipes NVS settings (Wi-Fi, preferences). The SD card is not affected.

## Monitor

```
make monitor    # 115200 baud
```

## Adding an app

Use `src/app/app_09/infrared.{h,cpp}` as the template.

1. Create `src/app/app_NN/` with your app class. Set a unique name (<= 31 chars) in the constructor; it identifies the app and is the label in the menu.
2. In `src/app/app.h`, `#include` the header and add one line to `APP_REGISTRY[]`: `{ &make<MOONCAKE::APPS::YourApp>, &ui_img_your_png },`. Table order = menu slot order.
3. If the icon is new, declare it with `LV_IMG_DECLARE` in `src/ui/ui.h` and add the image `.c` file (`tools/gen_app_icon.py` can generate simple glyph icons).

The apps menu scrolls vertically and holds up to `APPS_MENU_MAX_APPS` (24) apps; a `static_assert` in `app.h` fails the build if the registry grows beyond that. No changes to `ui_apps_menu.{c,h}` are needed.

Lifecycle notes:

- `onCreate` runs on first open only; `onClose` is the only teardown, so `onOpen` must re-initialise state.
- `onRunning` must return quickly; long-press B (exit) is only checked between updates.

## Infrared learn notes

- Learned signals are saved as Flipper `parsed` only when the protocol maps to a Flipper name (NEC, NECext, Samsung32, RC5, RC5X, SIRC, SIRC15, SIRC20) and `flipperEncodeFrame()` reproduces the decoded value/bits exactly; everything else is saved `raw`.
- Raw capture frequency is fixed at 38 kHz: the receiver module demodulates the carrier, so the real carrier cannot be measured.
- Raw captures that overflow the RX buffer are flagged "TRUNCATED" and cannot be saved.

## Shared BLE HID remote transport

- `src/app/app_common/ble_remote_hid.{h,cpp}` is the single BLE HID transport for Media Remote (app_10) and Slide Clicker (app_11). Namespace `ble_remote_hid`: `begin/end/tick/ready/connected/failed/tapKey/tapMedia`.
- Identity: GATT app id 0xB00, name "MeowKit Remote", static random address (Air Mouse scheme, distinct tweak) and one report map (keyboard report ID 1, consumer control report ID 2), so a host pairs once for both apps. It is intentionally not the BadUSB public-address identity.
- It takes a `bluetooth_manager` lease (`Owner::HidRemote`); with Bluetooth disabled in Settings `begin()` fails and `failed()` is true.
- Sending never blocks: `tapKey`/`tapMedia` queue a press+release pair (16-entry queue, a tap that does not fit is dropped and returns false) and `tick()` drains it with >= 10 ms between reports. Call `tick()` from every `onRunning`.
- `end()` sends a neutral keyboard and media report while the stack is valid, then releases the lease. Boot protocol mode carries keyboard reports only.

## Radio Scanner (app_12)

- Passive survey only: WiFi tab uses `WiFi.scanNetworks(async, hidden, passive, 120 ms)` every 5 s (not `ui_wifi_bridge`, which scans actively and drops the station); WiFi mode is saved on open and restored on close. If `settings_get_wifi_en()` is false it shows "WiFi disabled in Settings" and does not scan.
- BLE tab leases `bluetooth_manager` as `Owner::Scanner`, passive scan with duplicate filtering off. The GAP callback only updates a fixed 64-entry table under a `portMUX`; the UI snapshots it at 2 Hz and drops entries unseen for 30 s. Only one radio scans at a time; leaving the BLE tab releases the lease.

## Voice Recorder (app_13)

- Records 16 kHz mono 16-bit WAV to `/recordings/REC_YYYYMMDD_HHMMSS.wav` (PCF8563 time, `REC_NNNN` fallback when the RTC year is < 2024). Cap 30 min; needs >= 4 MB free. `wav_format.h` is a pure header (writer + bounds-checked parser) that host-compiles with plain `g++`.
- Audio bus: the mic (ES7210/I2S1) and speaker (ES8311/I2S0) share MCLK/BCLK/WS. `onOpen` calls `system_sound_suspend()` (shows "Audio busy" if the speaker is still enabled); the mic is open only while recording, the speaker only while playing; `onClose` is the only place that calls `system_sound_resume()` and always ends the speaker first.
- A core-0 task fills a 256 KB PSRAM ring (overflow is counted and shown as "DROPPED"); `onRunning` is the only SD writer (<= 16 KB per call) and patches the WAV header every 5 s and on stop, so a long-press-B exit or power loss leaves a playable file.
- Right opens the list (newest first); A plays/stops, B (tap) asks to delete, Left goes back.

## License

This repository is a mixed-license tree. JC Lima's original files are MIT-licensed (see [LICENSE-MIT](LICENSE-MIT)); upstream and third-party code keeps its own terms. [NOTICE.md](NOTICE.md) lists what is covered by what, including GPL considerations for distributed firmware binaries.
