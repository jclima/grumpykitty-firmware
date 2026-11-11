#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
if [ -n "${SCRATCH:-}" ]; then
  OUT=$SCRATCH; mkdir -p "$OUT"
  CLEAN=0
else
  OUT=$(mktemp -d /tmp/meowkit-ui-build.XXXXXX)
  CLEAN=1
fi
cleanup() { if [ "$CLEAN" -eq 1 ]; then rm -rf "$OUT"; fi; }
trap cleanup EXIT
CC=${CC:-cc}; CXX=${CXX:-c++}
SAN=${SAN:-}
INC="-DLV_CONF_INCLUDE_SIMPLE -DIRAM_ATTR= -I$ROOT/lib -I$ROOT/lib/lvgl/src -I$ROOT/tools/host_tests/ui -I$ROOT/src/ui -I$ROOT/src"
for f in $(find "$ROOT/lib/lvgl/src" -name '*.c' | sort); do
  o="$OUT/$(basename "$f" .c).o"
  "$CC" -std=c11 -O0 -g $SAN $INC -c "$f" -o "$o"
done
for f in "$ROOT/src/ui/screens/ui_t9_keyboard.c" "$ROOT/src/ui/screens/ui_wifi.c" "$ROOT/src/ui/screens/ui_sd_card_files.c" "$ROOT/src/ui/screens/ui_apps_menu.c" "$ROOT/src/ui/screens/ui_apps_manager.c" "$ROOT/src/ui/ui_helpers.c" "$ROOT/tools/host_tests/ui/bridge_stub.c" "$ROOT/tools/host_tests/ui/host_arduino.c" $(find "$ROOT/src/ui/images" "$ROOT/src/ui/fonts" -name '*.c' | sort); do
  o="$OUT/$(basename "$f" .c)-ui.o"
  "$CC" -std=c11 -O0 -g $SAN $INC -c "$f" -o "$o"
done
objs=$(find "$OUT" -name '*.o' -print)
"$CXX" -std=c++17 -O0 -g $SAN $INC "$ROOT/tools/host_tests/ui/main.cpp" $objs -lm $SAN -o "$OUT/meowkit-ui"
"$OUT/meowkit-ui"
