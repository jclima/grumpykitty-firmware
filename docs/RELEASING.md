<!-- SPDX-License-Identifier: MIT; Copyright (c) 2026 JC Lima -->
# Releasing

## One-time setup

- The repository must be **public** (GitHub Pages on private repos needs a paid plan).
- Settings > Pages > Source: **GitHub Actions**.
- Settings > Environments > `github-pages` > Deployment branches and tags > add a **Tag** rule `v*`. The release workflow deploys Pages from the tag ref; without this rule the deploy job fails immediately with no runner.
- The flasher then lives at `https://jclima.github.io/grumpykitty-firmware/flash/`.

## Version

The single source of truth is the `VERSION` file at the repo root. For the public OTA feed use `X.Y.Z-grumpy` with canonical numeric components and no leading `v`. The release tag is `vX.Y.Z-grumpy` and must match `VERSION`.

The firmware gets the same version at build time: `tools/gk_version.py` (a PlatformIO pre-script) reads `VERSION` and defines `GK_VERSION`, and `src/bsp/config.h` derives `FIRMWARE_VERSION` (`"v" GK_VERSION`, shown in Settings) from it. Do not hand-edit version strings elsewhere.

## Steps

1. Edit `VERSION`.
2. In `CHANGELOG.md`, rename `[Unreleased]` to `[X.Y.Z] - date` (the tag without `v`) and start a fresh `[Unreleased]`.
3. Commit, then tag and push to the GrumpyKitty repository. In a checkout where that remote is named `grumpy`:
   ```
   git tag vX.Y.Z-grumpy
   git push --atomic grumpy HEAD:refs/heads/main refs/tags/vX.Y.Z-grumpy
   ```
4. The `Release` workflow builds, runs host tests, packages the merged and split images, and verifies both the USB manifest and OTA feed before publishing the release and deploying the same files to Pages under `/flash/`. Verify the live `ota.json` version, size and SHA-256 against the live versioned firmware binary after deployment.

The on-device updater reads `/flash/ota.json` and downloads only the versioned `-firmware.bin` from the same HTTPS origin. The browser installer continues to use `manifest.json`. Keep the two metadata files and the versioned app binary in the same Pages deployment. Releases that change the bootloader or partition layout must be installed over USB because the updater writes only the inactive app slot.

## Local dry run

```
make dist     # builds first (make build), then writes dist/ (git-ignored)
```

`dist/` contains the merged image (flash at 0x0), versioned split images, `manifest.json` and `ota.json`. The merged CLI image is for esptool clean installs only: it contains 0xFF over the settings/NVS area, so flashing it wipes saved settings. The browser manifest writes the split regions at their existing offsets, preserving the settings/NVS partition unless the user explicitly selects erase; schema migrations may still require an erase or a firmware-provided migration. Flash locally with `python -m esptool --chip esp32s3 write_flash 0x0 dist/grumpykitty-X.Y.Z-grumpy-merged.bin`.

### Partition layout and size guard

`partitions_16mb.csv` defines two OTA slots: `app0` (0x10000) and `app1` (0x790000), 0x780000 (7.5 MB) each, plus an unused `spiffs` reserve and `coredump`. NVS (0x9000) and otadata (0xe000) never move, so saved settings survive. PlatformIO's size check compares against the largest app partition only, so `tools/release/merge_bin.py` parses the built `partitions.bin`, fails if `firmware.bin` exceeds the smallest OTA slot, warns at 90% of it, and fails if NVS or otadata have moved; `check_release.py` repeats the table and size checks on the shipped files.

A device cannot change its own partition table over the air. The first release with this table must be installed once through the browser flasher (Erase unticked, settings kept) or wired esptool; after that, the table is stable. If a flash is interrupted, re-enter Download Mode and flash again (the merged image is the recovery path if settings are expendable).

## Updating the vendored flasher library

The web flasher self-hosts ESP Web Tools in `docs/flash/vendor/esp-web-tools-<version>/` (the page has a strict CSP, no CDN). To update:

1. `curl -s https://registry.npmjs.org/esp-web-tools/<version>` and note `dist.tarball` and `dist.integrity`.
2. Download the tarball into a new empty directory and check it: `openssl dgst -sha512 -binary esp-web-tools-<version>.tgz | openssl base64 -A` must equal the integrity string (without the `sha512-` prefix). Stop if it differs.
3. `tar -xzf` it (never run package scripts). Check `package/dist/web/*.js` for remote imports, `eval` and `new Function` (the CSP has no `unsafe-eval`).
4. Replace the vendor directory with a copy of `package/dist/web/`, plus the package `LICENSE` and an updated `README-VENDOR.md` (version, tarball URL, integrity, date).
5. Update the `src` path in `docs/flash/index.html`, the table in `NOTICE.md`, and make sure the Pages step in `.github/workflows/release.yml` copies `docs/flash/flash.js` and `docs/flash/vendor/`.

## Licensing reminder

The firmware links GPL-3.0 (ESP32-audioI2S) and LGPL-2.1 code. Distributing binaries means offering the corresponding source: releases come from this public repository, and each tag is that source. See `NOTICE.md`.
