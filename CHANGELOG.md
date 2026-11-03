# Changelog

## [Unreleased]

### Added
- Table-driven, scrollable apps menu (up to 24 apps); adding an app is one line in `APP_REGISTRY[]`.
- Media Remote, Slide Clicker (BLE HID), Radio Scanner (Wi-Fi/BLE) and Voice Recorder (WAV on SD) apps.
- Shared BLE remote HID helper, SD guard helper and `tools/gen_app_icon.py` icon generator.

### Fixed
- Battery monitoring keeps running while an app is open.
- Race conditions in USB MSC teardown and system sound.
- Infrared: correct Flipper frame encoding, bounded `.ir` parsing, and learned signals saved with the right Flipper protocol names (including NECext) and an SD guard.
- Air Mouse uses the shared Bluetooth manager lease.
- Hardening of mic/VU task lifecycle, LVGL filesystem names and BadUSB script limits.
- Apps menu scrolling no longer overshoots content edges.

## [1.0.3] - 2026-10-22

- Improve Dino sound playback and audio resource handling.
- Refine VU meter rendering and Retro TV stream handling.
- Improve HID script parsing and input cleanup.
- Refine BLE task handling, settings display, and SD file list layout.
- Consolidate embedded system assets under src/system/assets.
