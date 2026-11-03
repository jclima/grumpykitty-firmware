# Changelog

## [Unreleased]

### Added
- Table-driven, scrollable apps menu (up to 24 apps); adding an app is one line in `APP_REGISTRY[]`.
- Media Remote, Slide Clicker (BLE HID), Radio Scanner (Wi-Fi/BLE) and Voice Recorder (WAV on SD) apps.
- Shared BLE remote HID helper, SD guard helper and `tools/gen_app_icon.py` icon generator.

### Changed
- New GrumpyKitty visual theme: mean alley-cat mascots, cat-gag app icons, brick-alley backgrounds, amber accents; colours centralised in src/ui/ui_theme.h
- Rebranded the firmware identity to GrumpyKitty (fork of MeowKit-S3): BLE advertised names and HID manufacturer, default BLE device name, USB MSC SCSI vendor id (`GrumpyKt`), About dialog name and serial log banners. Saved BLE names in NVS are not modified.

### Fixed
- "Updateing" typo on the update screen; Settings Wi-Fi label now fits its card (ellipsis instead of overflow).
- SD: a card that is inserted but cannot be mounted (exFAT, unformatted, non-FAT) is now reported as "unsupported format - use FAT32" instead of "No SD card"; failed remounts are throttled to once per 3 s.
- Battery monitoring keeps running while an app is open.
- Race conditions in USB MSC teardown and system sound.
- Infrared: correct Flipper frame encoding, bounded `.ir` parsing, and learned signals saved with the right Flipper protocol names (including NECext) and an SD guard.
- Air Mouse uses the shared Bluetooth manager lease.
- Hardening of mic/VU task lifecycle, LVGL filesystem names and BadUSB script limits.
- Apps menu scrolling no longer overshoots content edges.
- Battery: a failed AXP173 I2C read no longer yields garbage (invalid samples are ignored); critical shutdown needs 5 consecutive low readings and never happens on USB/charging; idle auto power-off is skipped while USB is connected.
- Wi-Fi: credentials are no longer stored in plaintext by the Arduino core (IDF config kept in RAM; legacy `nvs.net80211` data wiped on forget network and factory reset).
- Wi-Fi: clearing the saved password now really clears it; new credentials are saved only after a successful connection (a typo no longer overwrites working ones).
- Wi-Fi: scans are serialized (one scan task, results freed under a lock) and a connect aborts a running scan first.
- Retro TV: stale JPEG tables are reset per frame, a stalled frame is capped at 1 s and no longer blocks exiting with B, allocation failure handled without exceptions.
- I2C: read-modify-write sequences (bit ops, AXP173 setters, SHT30 read) now hold the bus lock for the whole sequence; AXP173 setters never write back after a failed read.
- BLE remote: keyboard and media reports are tracked separately (a media-only subscription no longer blocks keys) and a bonded host that reconnects without rewriting its CCCDs is treated as subscribed.
- Voice Recorder / VU Meter: a wedged capture task is no longer force-deleted (no double free); recording start no longer scans the whole FAT for free space; "Capture failed" shown if the capture task dies. USB MSC teardown leaks the card instead of freeing it under in-flight transfers.
- Infrared: `.ir` files are read in 512-byte chunks (much faster loading).
- Power: critical-battery shutdown now closes a running app first (its onClose runs, e.g. recording is finalised); a low-battery warning raised inside an app is shown as a toast when returning to the launcher.

## [1.0.3] - 2026-10-22

- Improve Dino sound playback and audio resource handling.
- Refine VU meter rendering and Retro TV stream handling.
- Improve HID script parsing and input cleanup.
- Refine BLE task handling, settings display, and SD file list layout.
- Consolidate embedded system assets under src/system/assets.
