#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 JC Lima
#
# Host (macOS/Linux) tests for the pure-C++ parts of the firmware. Builds into a
# temporary directory (override with SCRATCH=dir); nothing is written to the repo.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
if [ -n "${SCRATCH:-}" ]; then
  mkdir -p "$SCRATCH"
  OUT=$SCRATCH
else
  OUT=$(mktemp -d)
  trap 'rm -rf "$OUT"' EXIT
fi
CXX=${CXX:-c++}
$CXX -std=c++17 -Wall -Wextra -I lib/ArduinoJson/src \
  src/app/app_14/sk_model.cpp tools/host_tests/test_sk_model.cpp -o "$OUT/t"
"$OUT/t"

$CXX -std=c++17 -Wall -Wextra \
  tools/host_tests/test_wifi_pcap.cpp -o "$OUT/wifi_pcap"
"$OUT/wifi_pcap"
