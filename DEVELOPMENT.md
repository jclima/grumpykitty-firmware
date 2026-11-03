<!-- SPDX-License-Identifier: MIT; Copyright (c) 2026 JC Lima -->
# GrumpyKitty Development

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
- Identity: GATT app id 0xB00, name "GrumpyKitty Remote", static random address (Air Mouse scheme, distinct tweak) and one report map (keyboard report ID 1, consumer control report ID 2), so a host pairs once for both apps. It is intentionally not the BadUSB public-address identity.
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

## Boat Dash (app_14)

Read-only Signal K dashboard: GET only, it never writes to the server.

- `src/app/app_14/sk_model.{h,cpp}`: pure C++17 (no Arduino/ESP headers) port of the VesselStack display semantics: unit conversions and rounding, threshold severity with hysteresis, notification states, clock-free staleness, config parsing, the ArduinoJson filter for `vessels/self`. `sk_defaults.h` holds the built-in default config (each value cites its VesselStack source).
- `src/app/app_14/sk_client.{h,cpp}`: one worker task (12 KB stack, priority 1, core 0) that discovers the server and polls it. Shared state is heap-allocated and reference counted; `stop()` is cooperative and bounded (3.5 s). The worker is never `vTaskDelete`d mid-request. A worker stuck in a blocking connect finishes by itself and frees its state.
- `src/app/app_14/boat_dash.{h,cpp}`: the app (pages of 2x2 tiles, status page, alarm takeover).
- `lib/ArduinoJson` is vendored unmodified (version in `lib/ArduinoJson/VERSION`). `ARDUINOJSON_USE_DOUBLE` is already 1 on ESP32 and on the host, but the number parser still stores any literal whose mantissa fits 24 bits and whose exponent is small as float32 regardless of that macro (11.8 arrives as 11.8000001907). No configuration macro changes this, so `sk_model` reads every JSON number through `sk::jsonNumber`; do not call `as<double>()` on config or readings directly.

### Config file

`/grumpykitty/signalk.json` on the SD card. If the SD card or the file is missing the built-in defaults (`sk_defaults.h`) are used. If the file does not parse, the app shows the error with line and column for about 3 s and then uses the defaults. Format (the same schema as `sk_defaults.h`, which is the full example); a minimal file that only points at a server:

    {
      "schema": 1,
      "server": {"host": "192.168.1.50", "port": 3000, "token": "", "poll_ms": 2000, "fetch": "tree"}
    }

If you omit all of `instruments`, `notifications` and `pages`, the built-in ones are used with your `server`. If you give any of them, they replace the defaults (copy `sk_defaults.h` and edit it). `fetch` is `"tree"` (one GET of `vessels/self`) or `"per_path"` (one GET per configured path, 404 means absent). `poll_ms` is clamped to >= 1000. A page lists up to 4 tiles; longer item lists continue on extra pages. A `"special": "status"` page shows its first 3 items as rows plus server, link state, data age and the last error.

### Discovery and token

1. `server.host` if set. An explicit host is authoritative: if it does not answer `GET /signalk` the app shows "Server unreachable" and keeps retrying it; it does not wander to another server.
2. The last good `ip:port` cached in NVS (`sk_last_ip`).
3. mDNS service `_signalk-http._tcp` (2 s), then the host `vesselstack.local` (2 s). The device advertises itself as `grumpykitty` only while discovering.
4. Otherwise "Set host in /grumpykitty/signalk.json", retried every 8 s.

Each candidate must answer `GET /signalk` with `endpoints.v1`; the winner from step 3 is cached. After 3 failed polls on a discovered server it is discovered again. If the configured host answers 401/403 the app shows "Auth required: add token". The token goes in `server.token` and is sent only as `Authorization: Bearer ...`; it is never logged or displayed (it does sit in plain text on the SD card).

Token security: the token is sent ONLY to the configured `server.host`:`port`, never to a server found via mDNS or the NVS cache (mDNS can be spoofed on the LAN). With a token but no host, discovery runs without the token (a warning is logged at startup); if the discovered server answers 401/403 the app shows "Auth needs fixed host: set server.host". Signal K is plain HTTP here, so the token travels unencrypted on the boat LAN: use a read-only Signal K user/token. Always set a fixed `server.host` when you use a token.

### Using the app

Left/Right change page, short B returns to page 1, A refreshes now (or acknowledges the active alarm), long-press B exits. The header shows a cat-mood badge: `purr` all fresh values ok, `hmph` any warning, `HISS!` any alarm, `zzz` no fresh data. A new alarm jumps to its page, blinks the tile until A acknowledges it and beeps about 3 times a second (volume setting respected; the key-click toggle is ignored, so an alarm is audible even with key clicks off). The screen stays awake while an app is open (the launcher resets the idle timer in app mode). The status page, or any page with "STALE 42s" tiles, tells you when data has stopped arriving.

Host tests (macOS/Linux, no board, nothing written to the repo):

    tools/host_tests/run.sh                  # builds into a temp dir and runs; expects "N checks, 0 failed"
    SCRATCH=/some/dir tools/host_tests/run.sh   # keep the binary there instead

Mock Signal K server (Python 3 standard library only, read-only):

    python3 -I tools/mock_signalk.py --port 3000 --bind 0.0.0.0   # reachable from the device on your LAN
    python3 -I tools/mock_signalk.py --port 3000 --require-token SECRET --scenario warn
    curl -H 'Authorization: Bearer SECRET' http://127.0.0.1:3000/signalk/v1/api/vessels/self
    kill -USR1 <pid>                         # cycle ok -> warn -> alarm; or add ?scenario=alarm to any URL

It serves `/signalk` (discovery), `/signalk/v1/api/vessels/self` (full tree with `$source`/`values`/`meta` noise and a `Date` header) and per-path `/signalk/v1/api/vessels/self/<a>/<b>/...` (404 if unknown). Timestamps advance except for the frozen path `electrical.solar.main.current`, which goes stale on the dashboard. With `--require-token` the API answers 401 without the Bearer token (discovery stays open). The default `--bind` is loopback; use `--bind 0.0.0.0` so the device can reach it, and allow incoming connections for Python if the macOS firewall asks.

Acceptance checklist (on a device, with the mock server on your Mac):

- [ ] Open Boat Dash with no SD card: defaults load, "Connect WiFi in Settings" / "WiFi offline - A to reconnect" shown as appropriate.
- [ ] With `host` set to the Mac's IP: tiles fill within a poll or two, footer shows `ip:3000 | 2s`, badge `purr`.
- [ ] Solar tile goes `STALE nn s` and dims after ~30 s; badge stays `purr` for the rest.
- [ ] `kill -USR1` the mock twice: `hmph` (yellow borders) then `HISS!`: page jumps, tile blinks, beeps until A.
- [ ] A silences the beeps, the tile stays solid red; back to ok clears it; stop the mock: values age, status page shows "Server unreachable".
- [ ] `--require-token SECRET`: "Auth required: add token"; add the token to the config and it recovers.
- [ ] Malformed config: error with line/column for ~3 s, then defaults.
- [ ] No `host` and mock not discoverable: "Set host in /grumpykitty/signalk.json".
- [ ] Exit with long-press B while connected and while the server is unreachable; reopen repeatedly: no hang, no crash, heap stable.

## Art pipeline

The redone images under `src/ui/images/` (mascots, app icons, UI icons, backgrounds, panels) are
generated by `tools/art`; see `tools/art/README.md` for details. To regenerate and convert:

    tools/art/make_v3.sh                       # renders PNGs to tools/art/out/v3/alley/
    python3 tools/art/check_sizes_v3.py tools/art/out/v3/alley   # must report 0 mismatches
    for p in tools/art/out/v3/alley/ui_img_*.png; do
      n=$(basename "$p" .png)
      python3 tools/art/png_to_lvgl_c.py "$p" "src/ui/images/$n.c" "$n"
    done
    make build

Do not hand-edit those `.c` files; change `tools/art/draw_v3*.swift` and re-run. Keyboard, picker
and a few other images are still the original assets.

### UI palette

All UI colours live in `src/ui/ui_theme.h` (change the palette here): `THEME_*` 24-bit hex values for
LVGL (`THEME_LV(THEME_ACCENT)` wraps `lv_color_hex`) and matching `THEME_*_565` constants for
LovyanGFX app code. `ui.h` includes it for all screens; apps include `"ui_theme.h"`. If you change the
palette, also update the art palette in `tools/art` so the bitmaps match.

## License

This repository is a mixed-license tree. JC Lima's original files are MIT-licensed (see [LICENSE-MIT](LICENSE-MIT)); upstream and third-party code keeps its own terms. [NOTICE.md](NOTICE.md) lists what is covered by what, including GPL considerations for distributed firmware binaries.
