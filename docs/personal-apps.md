# Personal apps and app management

v0.1.7 introduces installable personal apps alongside the public utility and hacking apps.
Dino and the native Boat Dash dashboard and icons are excluded from the build.
The shared Signal K transport and data model remain available to personal apps.
This documents the supported package format and app-management behavior; it does not
claim that personal apps have been tested on hardware.

## Manage apps

Open **Apps → Manage**. Up/Down selects an app, Left/Right selects an action,
A activates it, and B returns. Touch controls are also available.

- **Hide / Show** changes the launcher without deleting a built-in app.
- **Move up / Move down** saves app order by permanent app ID.
- **Refresh** discovers personal packages copied to the SD card.
- **Remove** deletes a personal package and retains its data.
- **App + data** deletes the package and its `/appdata/<id>` directory.

Removal requires selecting the same action again for the same app. FW Update
stays visible. Built-in C++ applications cannot be individually unloaded from
the firmware image. Personal Lua packages can be installed and removed without
rebuilding firmware.

## Install a package

A package contains `manifest.json`, `main.lua`, and optional data files.
Copy it using a mounted microSD card or the firmware's USB SD mode:

```sh
python3 tools/install_personal_app.py examples/personal-app --sd-root /Volumes/YOUR_CARD
```

The installer validates the package, stages its copy, retains the previous
package until the replacement is in place, and preserves `/appdata/<id>`.
Safely eject the card or leave USB SD mode, then choose **Manage → Refresh**.
The firmware revalidates the package before every launch. This copy procedure
supports recovery after interrupted copying/renaming; it cannot guarantee
filesystem integrity after a power loss or unsafe card removal.

The private Boat Dash source lives in the Git-ignored `personal-apps/boat-dash`
directory in the owner's checkout. It is not included in public firmware or
release packaging. Install it using the same command with that source directory.
Its configuration belongs in `/appdata/boat-dash/signalk.json`. An existing
`/grumpykitty/signalk.json` remains a read-only fallback when the new file is absent.
Removing the app never deletes that legacy file.

## Package format

```json
{
  "id": "personal-utility",
  "name": "Personal Utility",
  "version": "1.0.0",
  "description": "A personal dashboard",
  "api_version": 1,
  "entry": "main.lua",
  "capabilities": []
}
```

IDs start with a lowercase ASCII letter or digit and contain only lowercase
letters, digits, `.`, `_`, and `-`. They are at most 31 bytes, contain no `..`, and
match the SD folder name. Metadata limits are 39 bytes for name, 15 for version,
and 95 for description. Manifests are at most 4 KiB; scripts are source text,
1 byte to 64 KiB. The installer accepts up to 64 entries, four path levels, and
1 MiB total. The firmware discovers up to 24 packages in the first 256 entries
of `/apps`; hidden staging directories are ignored. Native IDs are reserved.
Invalid packages appear in Manage with their error and cannot launch.

Declare only the capabilities used by the package:

| Capability | API |
| --- | --- |
| `wifi` | `mk.wifi.status()`, `mk.wifi.reconnect_saved()` |
| `sound` | `mk.sound.alert()`, `mk.sound.stop()` |
| `signalk` | `mk.signalk.open("defaults.json", "signalk.json")` |

UI and `mk.exit()` are always available. No general filesystem, process,
dynamic-loading, debug, or unrestricted networking API is exposed.

## Script lifecycle and graphics

`main.lua` returns a table with optional `open()` and `close()` functions and a
required `update(now_ms, input)` function. Input contains `left`, `right`, `up`,
`down`, and `a` press edges, plus a short `b` release. Buttons held while launching
must be released before generating app input. Hold B always exits through the
firmware. `now_ms` wraps at 2^32.

Use `mk.ui.clear()`, `header(title,status,tone)`, `footer(left,right)`,
`message(title,lines,tone)`, `tiles(items)`, and `rows(items)`. Drawing is buffered
in a PSRAM sprite and presented once after a callback that changes it. Tiles
support `label`, `value`, `unit`, `tone`, `stale`, and `blink`. Rows have `text`
and `tone`. Tones are `normal`, `dim`, `ok`, `warn`, and `alarm`. Text and widget
counts are bounded to the 320×240 display. Full-screen page changes should begin
with `clear()`. See `examples/personal-app/main.lua`.

One Lua VM runs at a time, with a 256 KiB allocation ceiling and a 100,000
instruction budget per protected host call. Lua base/table/string/math/utf8
libraries are available with dynamic loading, protected-call wrappers, metatable
mutation, print, and pattern matching removed. A script fault stops its resources
and shows an exit screen. These limits are for trusted personal scripts and are
not a security guarantee for arbitrary downloaded code. Native services and C
library work are not measured by the Lua instruction hook.

## Signal K bridge

`mk.signalk.open("defaults.json","signalk.json")` returns `session, layout`, or
`nil, error`. Defaults must be valid package-owned Signal K JSON. An optional
appdata configuration overrides top-level sections, then the entire result is
validated. A server-only override retains default instruments and pages. An
invalid override produces a visible warning and uses valid package defaults.

`layout.items` holds stable IDs and labels. `layout.pages` contains titles, item
IDs, and a status-page flag. `layout.config_source` and `layout.config_error`
identify the configuration result. Credentials are never returned to Lua.
Sessions provide `snapshot()`, `refresh()`, and `close()`; only one session may
be opened per app launch.

Snapshots preserve item order and include display value, unit, severity,
missing/stale state, notification/threshold flags, age, and server status.
Networking uses the existing read-only HTTP polling worker. Closing or faulting
stops the session; the launcher waits for outstanding work before starting
another app or removing files.

## Validation boundary

Host tests cover catalog preferences, package validation, installer recovery,
Lua fault containment, and selected UI interactions. The local Boat Dash fixture
covers pages, stale readings, alarm acknowledgement/rearming, reconnect, and
configuration fallback. Physical MeowKit checks for readability, sound, SD/USB
transitions, long-B exit, and preservation across a real OTA update remain
necessary.
