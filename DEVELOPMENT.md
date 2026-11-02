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

1. Create `src/app/app_NN/` with your app class. Set a unique name (<= 31 chars) in the constructor; it identifies the app in the menu.
2. In `src/app/app.h`, `#include` the header and add an `installApp()` call in `registerAllApps()` after the last one (order = menu slot index).
3. Add the icon at the matching index of `APP_BUILTIN_ICONS[]` in the same file.
4. Declare the icon with `LV_IMG_DECLARE` in `src/ui/ui.h` and add the image `.c` file.
5. Raise `APPS_MENU_MAX_APPS` (currently 9) in `src/ui/screens/ui_apps_menu.h`, and in `src/ui/screens/ui_apps_menu.c` add `ui_app_N` / `ui_name_N` (definitions, both `app_ptrs[]` and `name_ptrs[]` arrays, and a row in `_app_info[]`).

Lifecycle notes:

- `onCreate` runs on first open only; `onClose` is the only teardown, so `onOpen` must re-initialise state.
- `onRunning` must return quickly; long-press B (exit) is only checked between updates.
