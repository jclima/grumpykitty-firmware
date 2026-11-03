<!-- SPDX-License-Identifier: MIT; Copyright (c) 2026 JC Lima -->
# Releasing

## One-time setup

- The repository must be **public** (GitHub Pages on private repos needs a paid plan).
- Settings > Pages > Source: **GitHub Actions**.
- The flasher then lives at `https://jclima.github.io/grumpykitty-firmware/flash/`.

## Version

The single source of truth is the `VERSION` file at the repo root (plain `X.Y.Z`). The release workflow takes the version from the tag (`vX.Y.Z` -> `X.Y.Z`), so keep the two equal.

Follow-up (not done yet): the version string compiled into the firmware (`platformio.ini` header, About dialog, serial banner) is still hand-written. Feed it from `VERSION` at build time, for example in `tools/embed_gif.py` or a new pre-script emitting `-DGK_VERSION`.

## Steps

1. Edit `VERSION`.
2. In `CHANGELOG.md`, rename `[Unreleased]` to `[X.Y.Z] - date` and start a fresh `[Unreleased]`.
3. Commit, then tag and push:
   ```
   git tag vX.Y.Z
   git push origin main vX.Y.Z
   ```
4. The `Release` workflow builds, runs host tests, creates `grumpykitty-X.Y.Z-merged.bin`, `-firmware.bin`, `SHA256SUMS` and `manifest.json`, publishes a GitHub Release, and deploys `docs/flash` plus the merged image and manifest to Pages.

## Local dry run

```
make dist     # builds first (make build), then writes dist/ (git-ignored)
```

`dist/` contains the merged image (flash at 0x0), the app-only image and `manifest.json`. Flash locally with `python -m esptool --chip esp32s3 write_flash 0x0 dist/grumpykitty-X.Y.Z-merged.bin`.

## Licensing reminder

The firmware links GPL-3.0 (ESP32-audioI2S) and LGPL-2.1 code. Distributing binaries means offering the corresponding source: releases come from this public repository, and each tag is that source. See `NOTICE.md`.
