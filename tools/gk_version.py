"""
PlatformIO pre-build extra script.
Reads the repo-root VERSION file (the single version source) and defines
GK_VERSION as a string literal (e.g. "0.1.0-grumpy") for every translation unit.
src/bsp/config.h builds FIRMWARE_VERSION from it.
"""
Import("env")  # noqa: F821  (PlatformIO SCons magic)
import os
import re

version_file = os.path.join(env.subst("$PROJECT_DIR"), "VERSION")  # noqa: F821
with open(version_file, encoding="utf-8") as f:
    version = f.read().strip()
if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?", version):
    raise SystemExit(f"gk_version.py: invalid VERSION {version!r} (expected X.Y.Z or X.Y.Z-suffix)")

# StringifyMacro adds the quoting/escaping that the shell and compiler need.
env.Append(CPPDEFINES=[("GK_VERSION", env.StringifyMacro(version))])  # noqa: F821
