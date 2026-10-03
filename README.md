# YapNotifier

A TeamSpeak 3 overlay for GTA San Andreas multiplayer (SA-MP). Shows who is in your channel, who is talking, muted or away, plus join/leave/whisper notifications and a chat feed, drawn inside the game.

Two parts, one release:

| File | Goes where | Does |
|------|-----------|------|
| `YapNotifierSA.asi` | GTA San Andreas folder (or `scripts\`) | Draws the overlay and the config menu |
| `YapNotifier.ts3_plugin` | TeamSpeak 3 client | Sends your channel state to the overlay over localhost UDP (port 25640) |

## Install

1. Download both files from the [latest release](https://github.com/shadyamr/yapnotifierSA/releases/latest).
2. **Overlay:** copy `YapNotifierSA.asi` into your GTA San Andreas folder (where `gta_sa.exe` is) or its `scripts` folder. You need an ASI loader: Silent's ASI Loader, or the one that comes with CLEO. GTA SA must be version 1.0 US, which SA-MP needs anyway.
3. **TeamSpeak plugin:** double-click `YapNotifier.ts3_plugin`. TeamSpeak installs it and asks to enable it (or enable it under *Tools → Options → Addons*).
4. Start SA-MP. Join a channel in TeamSpeak and the roster appears.

The overlay checks GitHub for a new version on every launch and installs it for the next start (`auto_update=0` in the INI turns installing off). The TeamSpeak plugin is not auto-updated; grab it from the release when the overlay tells you it is outdated.

## Use

- **INSERT** opens the config menu (rebind it on the menu's Keys tab, or `menu_key` in the INI as a Windows virtual-key code). While it is open the game's camera is locked and the cursor is shown; close it with INSERT again or the menu's ×.
- **HOME** hides/shows the overlay (rebind it on the Keys tab or via `hide_key`; also a checkbox on the Status tab). Hidden is remembered when you Save.
- **Status tab → Demo mode** renders a fake roster and scripted events so you can lay everything out without TeamSpeak running.
- Tabs: Status, Layout (anchor, offsets, scale, font), Roster (visibility, sorting, colours), Indicators (per-state icons/colours), Notifications, Chat, Users (per-person overrides), Channel (per-channel overrides), Profiles (save/load whole configs).

Edits apply immediately; **Save** writes them. Only changing the font file needs a restart.

Files, next to the `.asi`:

- `YapNotifier.ini` — settings (`[YapNotifier]`, plus `[user:<unique id>]` and `[channel:<id>]` overrides)
- `YapNotifier.profiles\*.ini` — saved profiles
- `YapNotifier.log` — runtime log; first place to look when something is off

## Notes

- Only the **active** TeamSpeak tab is mirrored.
- SA-MP 0.3.DL: the menu uses SA-MP's cursor, and the hotkeys are ignored while you're typing in chat or a dialog is open. Other SA-MP versions and single-player work too, with the overlay's own cursor. SA-MP's chat and dialogs draw on top of the overlay.
- If the roster shows *TS3 plugin outdated*, update `YapNotifier.ts3_plugin`.
- If nothing shows up: check `YapNotifier.log` next to the `.asi` (it says whether the game hook was installed), make sure an ASI loader is installed and GTA SA is 1.0 US, and that nothing else is bound to UDP 25640.

## Build from source

Requires Visual Studio 2022 (MSVC, x86 and x64 toolsets) and CMake ≥ 3.24. Dependencies (RmlUi, FreeType, LunaSVG, TS3 plugin SDK) are fetched by CMake on first configure.

```
cmake -S . -B build -A Win32 [-DYAPNOTIFIER_DEPLOY_DIR="C:/path/to/GTA San Andreas"]
cmake --build build --config Release
ctest --test-dir build -C Release
```

Outputs `build/Release/YapNotifierSA.asi` and `build/YapNotifier.ts3_plugin`. With `YAPNOTIFIER_DEPLOY_DIR` set, the `.asi` is copied there after every build.

**Releasing:** bump `YAP_VERSION` / `YAP_VERSION_NUM` in `shared/version.h` and `Version` in `ts3plugin/package.ini`, push a `v<version>` tag. CI builds, tests and publishes the GitHub Release.

See `CLAUDE.md` for the architecture and `docs/superpowers/specs/` for the design documents.

## Credits

- Contact/friend lookup from the TeamSpeak `settings.db` is adapted from TeamSpeak3-Reshade-overlay (MIT, `assets/LICENSE-tsro.txt`).
- UI font: [Inter](https://rsms.me/inter/) (SIL OFL, `assets/OFL-Inter.txt`).
- Rendering: [RmlUi](https://github.com/mikke89/RmlUi).
