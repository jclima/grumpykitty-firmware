# GrumpyKitty

Community firmware for the MeowKit-S3 (ESP32-S3-WROOM-1-N16R8): more apps, bug fixes and a grumpy alley-cat theme. Unofficial fork of the MeowKit-S3 firmware by Mingo / Whitecliff, see [NOTICE.md](NOTICE.md).

| Home | Apps | Dialogs | Update |
|---|---|---|---|
| ![Home](docs/images/home.png) | ![Apps menu](docs/images/apps_menu.png) | ![Message box](docs/images/message_box.png) | ![Update](docs/images/update.png) |

## Features

- All the vendor apps, plus a scrollable apps menu (up to 24 apps).
- **Media Remote** and **Slide Clicker**: BLE HID remotes.
- **Radio Scanner**: passive Wi-Fi and BLE survey.
- **Voice Recorder**: WAV files on the SD card.
- **Boat Dash**: read-only Signal K dashboard with alarms.
- Infrared: Flipper `.ir` files parsed and saved correctly (bounded parsing, proper protocol names).
- Fixes: SD cards in unsupported formats are reported clearly, battery monitoring and shutdown logic made robust, Wi-Fi credentials no longer stored in plaintext, race conditions in USB MSC and system sound, UI typos and overflows. Full list in [CHANGELOG.md](CHANGELOG.md).

## Flash it

Always power the device **off** first and enter Download Mode (below). Flashing does not touch the SD card.

**a) From the browser (easiest).** Desktop Chrome or Edge: <https://jclima.github.io/grumpykitty-firmware/flash/> (available once GitHub Pages is enabled for the repository).

**b) With esptool.** Download `grumpykitty-X.Y.Z-merged.bin` from [Releases](https://github.com/jclima/grumpykitty-firmware/releases):

```
python -m esptool --chip esp32s3 write_flash 0x0 grumpykitty-X.Y.Z-merged.bin
```

**c) From source** (needs PlatformIO, see [DEVELOPMENT.md](DEVELOPMENT.md)):

```
make bootstrap-check && make upload
```

### Download Mode

1. Power off. Push a Dupont jumper wire through the hole on the back to hold BOOT.
2. While holding BOOT, hold Power for 1-2 s.
3. Release Power, then release BOOT. The screen stays off.
4. Connect USB-C directly to the computer (no hub) with a data cable. The port shows as "USB JTAG/serial debug unit".

### After flashing

Hold Power for 4-5 s to power off, then press Power to boot (or briefly short RESET to GND). A full-chip erase wipes saved settings such as Wi-Fi; a normal update from MeowKit or GrumpyKitty keeps them.

## SD card

Format the card as **FAT32 with an MBR** partition table (exFAT is not supported), then copy the contents of the `sd files/` folder to its root. On macOS: Disk Utility > View > Show All Devices > select the card (the device, not the volume) > Erase > Format `MS-DOS (FAT)`, Scheme `Master Boot Record`.

## Boat Dash quick start

Create `/grumpykitty/signalk.json` on the SD card (or rely on mDNS discovery) and open Boat Dash from the apps menu. It is read-only and sends no commands. Config keys, the mock server and tokens are covered in [DEVELOPMENT.md](DEVELOPMENT.md).

## Building and contributing

`make build`, `make upload`, `make monitor` and `make dist` are described in [DEVELOPMENT.md](DEVELOPMENT.md); releases in [docs/RELEASING.md](docs/RELEASING.md). Bug reports and pull requests are welcome, use the templates. Security issues: see [SECURITY.md](SECURITY.md).

## License

Mixed licensing, read [NOTICE.md](NOTICE.md). In short:

- JC Lima's contributions are MIT ([LICENSE-MIT](LICENSE-MIT)).
- Upstream vendor code carries no explicit license and is not relicensed here.
- Third-party libraries keep their own licenses. ESP32-audioI2S (GPL-3.0) is linked into the firmware, so anyone distributing binaries must offer the corresponding source; IRremoteESP8266 is LGPL-2.1.
- "MeowKit" is the vendor's trademark. This is an unofficial community fork, not affiliated with or endorsed by the vendor.

Use security-related features (BadUSB, infrared, radio scanning) only on devices you own or are authorized to test.

## Credits

- [MeowKit-S3 firmware](https://github.com/mingolucky/meowkit-s3-firmware) by Mingo / Whitecliff Tech, the upstream project. Hardware docs, setup and product documentation: [docs.meowkit.cc](https://docs.meowkit.cc/).
- Libraries: LVGL, LovyanGFX, Mooncake, ArduinoJson, AnimatedGIF, ESP32-audioI2S, IRremoteESP8266, Lua, ESP32-BLE-Keyboard/Mouse and others under `lib/`.
- Web flashing: [ESP Web Tools](https://github.com/esphome/esp-web-tools) (Apache-2.0).
