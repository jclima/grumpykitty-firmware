#!/usr/bin/env python3
"""Install a personal app on a mounted microSD card, preserving its user data."""
import argparse
import json
from pathlib import Path
import re
import shutil
import tempfile


def inspect_package(source):
    source = Path(source)
    if not source.is_dir() or source.is_symlink():
        raise ValueError("Package must be a real directory")
    files = list(source.rglob("*"))
    if len(files) > 64:
        raise ValueError("Package exceeds 64 entries")
    total = 0
    for item in files:
        if item.is_symlink() or not (item.is_file() or item.is_dir()):
            raise ValueError("Package contains a link or special file")
        if len(item.relative_to(source).parts) > 4:
            raise ValueError("Package directories are too deep")
        if item.is_file():
            total += item.stat().st_size
    if total > 1024 * 1024:
        raise ValueError("Package exceeds 1 MiB")
    manifest_path = source / "manifest.json"
    if manifest_path.stat().st_size > 4096:
        raise ValueError("Manifest exceeds 4 KiB")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict):
        raise ValueError("Manifest must be an object")
    app_id = manifest.get("id", "")
    if not isinstance(app_id, str) or not re.fullmatch(r"[a-z0-9][a-z0-9._-]{0,30}", app_id) or ".." in app_id:
        raise ValueError("Invalid app ID")
    if type(manifest.get("api_version")) is not int or manifest["api_version"] != 1 or manifest.get("entry") != "main.lua":
        raise ValueError("Expected app API 1 and main.lua entry")
    for key, limit in (("name", 39), ("version", 15), ("description", 95)):
        if not isinstance(manifest.get(key), str) or not manifest[key] or len(manifest[key].encode("utf-8")) > limit:
            raise ValueError(f"Manifest needs {key}")
    caps = manifest.get("capabilities")
    if not isinstance(caps, list) or len(caps) > 3 or any(not isinstance(c, str) or c not in ("wifi", "sound", "signalk") for c in caps) or len(set(caps)) != len(caps):
        raise ValueError("Capabilities must list unique wifi, sound, or signalk permissions")
    script = source / "main.lua"
    if not script.is_file() or not 0 < script.stat().st_size <= 65536:
        raise ValueError("main.lua must be between 1 byte and 64 KiB")
    if script.read_bytes().startswith(b"\x1bLua"):
        raise ValueError("Use Lua source text")
    return app_id


def install(source, sd_root):
    app_id = inspect_package(source)
    source = Path(source).resolve()
    sd_root = Path(sd_root)
    if not sd_root.is_dir():
        raise ValueError("Choose the root of an already mounted microSD card")
    apps = sd_root / "apps"
    if apps.is_symlink():
        raise ValueError("The apps directory must not be a link")
    apps.mkdir(exist_ok=True)
    target = apps / app_id
    backup = apps / f".{app_id}.previous"
    for item in (target, backup):
        if item.is_symlink() or (item.exists() and not item.is_dir()):
            raise ValueError("App destination must be a real directory")
    if source == target.resolve() or target.resolve() in source.parents or source in target.resolve().parents:
        raise ValueError("Source and destination must be separate directories")
    # Restore a prior version if an interrupted replacement left it staged.
    if backup.exists():
        if target.exists():
            raise ValueError(f"Recovery copy exists at {backup}; retain or remove it before updating")
        backup.rename(target)
    staged = Path(tempfile.mkdtemp(prefix=f".{app_id}.install-", dir=apps))
    try:
        shutil.copytree(source, staged, dirs_exist_ok=True)
        inspect_package(staged)
        if target.exists():
            target.rename(backup)
        try:
            staged.rename(target)
        except OSError:
            if backup.exists():
                backup.rename(target)
            raise
        if backup.exists():
            shutil.rmtree(backup)
    finally:
        if staged.exists():
            shutil.rmtree(staged)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--sd-root", required=True, type=Path)
    args = parser.parse_args()
    try:
        installed = install(args.package, args.sd_root)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Install failed: {error}\n")
    print(f"Installed {installed}. Safely eject the card, then refresh SD apps on MeowKit.")


if __name__ == "__main__":
    main()
