# Changelog

## [Unreleased]

### Fixed
- Reject malformed JPEG input in the Retro TV decoder: check DQT/DHT lengths, DC category and AC zero-run bounds, initialise the block flag, validate SOF0/DRI/SOS segment lengths, reset the decoder object before each frame and report a failed listen socket.
- Skip SD probe/remount while mass storage is active or draining, ignore the Settings storage button during a screen animation, add usb_msc_is_quiescent() and show a sticky 'Eject on computer first' notice instead of releasing mid-write.

## [0.1.8-grumpy] - 2026-11-11

### Fixed
- Wi-Fi setup keeps Up/Down selection visible, activates the selected network with A, and refreshes safely after rescans or screen changes.

## [0.1.7-grumpy] - 2026-11-10

### Added
- Apps -> Manage keeps a stable order and visibility for built-in and personal apps.
- Personal apps can be installed from supported Lua packages on microSD and use the Signal K bridge.

### Changed
- Dino is excluded from the public firmware build, and Boat Dash is maintained as a separate private personal app.
- Apps management, SD recovery, and related UI flows are clearer and more resilient.
- OTA clock synchronization allows a longer SNTP wait before reporting failure.

## [0.1.6-grumpy] - 2026-11-10

### Fixed
- FW Update redraws only when status, hint, or displayed progress changes, with a complete-frame sprite path to prevent screen flicker.
- FW Update reports clearer DNS, connection, TLS, and HTTP failures; if OTA cannot complete, use the USB browser flasher to install the release.

## [0.1.5-grumpy] - 2026-11-10

### Fixed
- Wi-Fi setup keeps the selected network when a password attempt fails, supports more password characters, and stops delayed navigation when you leave the screen.
- SD browsing refreshes when media changes, pages through large directories, and shows clearer USB storage status.
- Infrared imports have size limits, and saving checks that the complete file reached the card before reporting success.
- Air Mouse initialization reports sensor failures, and VU Meter retains the audio hardware until its capture task exits.
- BLE scanning and remote controls recover more clearly from startup errors. BLE HID scripts reject unsupported commands before sending reports.
- Clock and Settings use the same RTC, invalid dates are rejected, and Signal K reports authentication failures during discovery.

## [0.1.4-grumpy] - 2026-11-10

### Added
- FW Update checks for newer GrumpyKitty releases over HTTPS, verifies the download, and stages it in the inactive firmware slot. Install this first OTA-capable version via the USB browser flasher.
- The release workflow publishes an OTA feed tied to the exact app image and validates it before deployment.

## [0.1.3-grumpy] - 2026-11-10

### Added
- Radio Scanner Plus adds Wi-Fi channel counts, signal history for a selected Wi-Fi or BLE device, CSV export to microSD, and the Grumpy Scout survey view.
- Radio Scanner Plus adds a passive Wi-Fi management frame monitor with explicit Start and Stop, channel selection when disconnected, and optional filtered PCAP files on microSD.

## [0.1.2-grumpy] - 2026-11-09

### Changed
- Partition table now has two 7.5 MB OTA slots (`app0`, `app1`) in preparation for over-the-air updates; NVS and otadata offsets are unchanged. Installing this release needs one more browser or wired flash (Erase unticked, settings are kept); a device cannot change its partition table over the air. Release tooling fails the build if the firmware exceeds the smallest slot. No OTA update feature exists yet: this release only lays out the slots. Devices move to the new layout by a browser flash with Erase unticked, which keeps settings.

### Fixed
- Settings written by a newer firmware (schema version above this build's) are no longer erased when an older build runs, e.g. after a rollback.
- VU Meter now enters only after system audio releases the shared bus; the handoff cancels the current system sound, and if the bus cannot be released in time the app is refused with an actionable error.
- USB storage teardown keeps SD ownership until pending callbacks finish; Voice Recorder drains capture safely.
- Wi-Fi scan/connect handoff is serialized: a cancelled scan and a new selection can no longer overlap, the UI reports a queued connect, and the radio-busy case is guarded.
- Settings flush is retryable if a save fails, and the Wi-Fi password is stored without a plaintext copy; leftover plaintext credentials are scrubbed.
- Closing an app now shows a "closing" toast in the launcher while the app shuts down.
- BSP/LVGL filesystem errors are surfaced safely instead of corrupting state; the SD mount frequency was passed to the driver 1000x too high (kHz scaled to Hz for a kHz API) and is corrected.

## [0.1.1-grumpy] - 2026-11-08

### Fixed
- Release packaging now publishes split web-flasher images that preserve NVS/settings during normal updates.
- The web flasher no longer claims settings survive an update from stock MeowKit firmware (untested); the merged image is documented as a settings-wiping clean install.

### Changed
- Firmware version shown in Settings now comes from the `VERSION` file at build time (`tools/gk_version.py`).
- The release workflow verifies the assembled flasher site (`tools/release/check_release.py`) before publishing.

## [0.1.0-grumpy] - 2026-11-02

### Added
- Table-driven, scrollable apps menu (up to 24 apps); adding an app is one line in `APP_REGISTRY[]`.
- Media Remote, Slide Clicker (BLE HID), Radio Scanner (Wi-Fi/BLE) and Voice Recorder (WAV on SD) apps.
- Boat Dash app: read-only Signal K dashboard (2x2 tile pages, alarm takeover with sound, status page, config in `/grumpykitty/signalk.json`, server discovery via mDNS), plus a mock Signal K server and host tests.
- Shared BLE remote HID helper, SD guard helper and `tools/gen_app_icon.py` icon generator.

### Changed
- New GrumpyKitty visual theme: mean alley-cat mascots, cat-gag app icons, brick-alley backgrounds, amber accents; colours centralised in src/ui/ui_theme.h
- Rebranded the firmware identity to GrumpyKitty (fork of MeowKit-S3): BLE advertised names and HID manufacturer, default BLE device name, USB MSC SCSI vendor id (`GrumpyKt`), About dialog name and serial log banners. Saved BLE names in NVS are not modified.
- New boot animation: GrumpyKitty alley cat wakes in the dark, yawns, hisses and shows a MEH bubble and the GrumpyKitty wordmark (23 frames, 3.16 s, 113 KB instead of 2.1 MB; frame-delta GIF with transparency). The boot sound is trimmed to the new duration. Regenerate with `tools/art/make_boot_gif.sh`.

### Security
- Boat Dash: the Signal K bearer token is sent only to the configured `server.host`, never to servers found via mDNS or the cached address. With a token but no host the app shows "Auth needs fixed host: set server.host" on 401/403.
- Boat Dash: the Signal K token is never sent to a `.local` (mDNS) `server.host`, since its resolution is spoofable on the LAN; the status page shows "Token needs IP host (not .local)". Use an IP or unicast DNS name. Signal K is plain HTTP (no HTTPS yet), so use a read-only Signal K user.
- Boat Dash: `server.host` is now strictly validated (hostname or IPv4 only; no port, userinfo, path, query, fragment or whitespace), closing a parser differential that let values like `vesselstack.local:3000` or `x@vesselstack.local` slip past the `.local` token guard. Invalid hosts fall back to the built-in defaults (discovery, no token).

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
