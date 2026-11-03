<!-- SPDX-License-Identifier: MIT; Copyright (c) 2026 JC Lima -->
# Releasing

## One-time setup

- The repository must be **public** (GitHub Pages on private repos needs a paid plan).
- Settings > Pages > Source: **GitHub Actions**.
- The flasher then lives at `https://jclima.github.io/grumpykitty-firmware/flash/`.

## Version

The single source of truth is the `VERSION` file at the repo root (plain `X.Y.Z` or `X.Y.Z-suffix`, for example `0.1.0-grumpy`; no leading `v`). The release workflow takes the version from the tag (`vX.Y.Z` -> `X.Y.Z`) and fails early if it differs from `VERSION`, so keep the two equal.

Follow-up (not done yet): the version string compiled into the firmware (`platformio.ini` header, About dialog, serial banner) is still hand-written. Feed it from `VERSION` at build time, for example in `tools/embed_gif.py` or a new pre-script emitting `-DGK_VERSION`.

## Steps

1. Edit `VERSION`.
2. In `CHANGELOG.md`, rename `[Unreleased]` to `[X.Y.Z] - date` (the tag without `v`) and start a fresh `[Unreleased]`.
3. Commit, then tag and push:
   ```
   git tag vX.Y.Z
   git push origin main vX.Y.Z
   ```
4. The `Release` workflow builds, runs host tests, creates `grumpykitty-X.Y.Z-merged.bin`, `-firmware.bin`, `SHA256SUMS` and `manifest.json`, publishes a GitHub Release, and deploys the whole `docs/flash` folder plus the merged image and manifest to Pages (`/flash/`, with a root page redirecting there).

## Local dry run

```
make dist     # builds first (make build), then writes dist/ (git-ignored)
```

`dist/` contains the merged image (flash at 0x0), the app-only image and `manifest.json`. Flash locally with `python -m esptool --chip esp32s3 write_flash 0x0 dist/grumpykitty-X.Y.Z-merged.bin`.

## Updating the vendored flasher library

The web flasher self-hosts ESP Web Tools in `docs/flash/vendor/esp-web-tools-<version>/` (the page has a strict CSP, no CDN). To update:

1. `curl -s https://registry.npmjs.org/esp-web-tools/<version>` and note `dist.tarball` and `dist.integrity`.
2. Download the tarball into a new empty directory and check it: `openssl dgst -sha512 -binary esp-web-tools-<version>.tgz | openssl base64 -A` must equal the integrity string (without the `sha512-` prefix). Stop if it differs.
3. `tar -xzf` it (never run package scripts). Check `package/dist/web/*.js` for remote imports, `eval` and `new Function` (the CSP has no `unsafe-eval`).
4. Replace the vendor directory with a copy of `package/dist/web/`, plus the package `LICENSE` and an updated `README-VENDOR.md` (version, tarball URL, integrity, date).
5. Update the `src` path in `docs/flash/index.html`, the table in `NOTICE.md`, and make sure the Pages step in `.github/workflows/release.yml` copies `docs/flash/flash.js` and `docs/flash/vendor/`.

## Licensing reminder

The firmware links GPL-3.0 (ESP32-audioI2S) and LGPL-2.1 code. Distributing binaries means offering the corresponding source: releases come from this public repository, and each tag is that source. See `NOTICE.md`.
