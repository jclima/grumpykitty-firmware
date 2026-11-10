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

$CXX -std=c++17 -Wall -Wextra -I lib/ArduinoJson/src \
  tools/host_tests/test_ota_manifest.cpp -o "$OUT/ota_manifest"
"$OUT/ota_manifest"

$CXX -std=c++17 -Wall -Wextra \
  tools/host_tests/test_infrared_limits.cpp -o "$OUT/infrared_limits"
"$OUT/infrared_limits"

for suite in calendar hardware_protocol ir_loader ir_transaction; do
  $CXX -std=c++17 -Wall -Wextra \
    "tools/host_tests/test_${suite}.cpp" -o "$OUT/$suite"
  "$OUT/$suite"
  echo "$suite: PASS"
done

python3 tools/host_tests/test_ir_parser.py
echo "ir_parser: PASS"

tools/host_tests/ui/run_ui.sh
echo "host tests: PASS"
