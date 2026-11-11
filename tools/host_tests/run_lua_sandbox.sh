#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
OUT=${SCRATCH:-$(mktemp -d)}
mkdir -p "$OUT"
trap '[ -n "${SCRATCH:-}" ] || rm -rf "$OUT"' EXIT
CXX=${CXX:-c++}
CC=${CC:-cc}
LUA="$ROOT/lib/lua-5.4.7/src"
CORE="lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lcorolib ldblib liolib lmathlib loadlib loslib lstrlib ltablib lutf8lib linit"
objs=""
for f in $CORE; do $CC -std=gnu99 -I"$LUA" -c "$LUA/$f.c" -o "$OUT/$f.o"; objs="$objs $OUT/$f.o"; done
$CXX -std=c++17 -Wall -Wextra -I"$LUA" $objs \
  "$ROOT/src/app/personal_apps/lua_sandbox.cpp" \
  "$ROOT/tools/host_tests/test_lua_sandbox.cpp" -lm -o "$OUT/lua_sandbox"
"$OUT/lua_sandbox"
echo "lua_sandbox: PASS"
