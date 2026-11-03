<!-- SPDX-License-Identifier: MIT; Copyright (c) 2026 JC Lima -->
# Security policy

## Reporting a vulnerability

Please report security issues **privately** through GitHub: open the repository's **Security** tab and choose **Report a vulnerability** (GitHub private security advisories). Do not open a public issue for vulnerabilities.

Include the firmware version, what you observed, and steps to reproduce. This is a hobby project maintained on a best-effort basis; expect an acknowledgement but no fixed timeline.

## Scope

In scope: code in this repository that ships in the firmware (for example Boat Dash credential handling, Wi-Fi credential storage, BLE/USB HID features, SD file parsing) and the release and flasher tooling.

Out of scope: the vendor's hardware and upstream code unchanged by this fork (report to the upstream project), third-party libraries (report to them), and attacks needing physical access to an unlocked device. The security-related tools in the firmware (BadUSB, IR, Radio Scanner) are meant for devices you own or are authorized to test.
