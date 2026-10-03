# YapNotifier: FiveM -> GTA San Andreas / SA-MP port (design)

## Context

YapNotifier currently ships an x64 `.asi` for FiveM. It draws RmlUi in its own transparent topmost window with D3D11 + DirectComposition and deliberately uses no game hooks, because FiveM's anti-cheat kills hooked processes. The user wants the project moved to GTA San Andreas multiplayer (SA-MP). This is a **replacement**: FiveM support is dropped, and 0.4.1 is the last FiveM release.

Decisions made with the user:
- **Replace FiveM.** No dual target and no fork.
- **Draw in-game**, because players run a mix of exclusive fullscreen and windowed modes. A separate topmost window is invisible over exclusive-fullscreen D3D9, so we hook the game's frame and render RmlUi into its D3D9 back buffer with our own small fixed-function renderer (approach 1 of 3; the D3D11-offscreen-readback and windowed/fullscreen-hybrid approaches were rejected).
- **Light SA-MP integration, 0.3.DL R1 only:** use SA-MP's cursor/camera lock, and suppress hotkeys while its chat input or a dialog is open. On any other `samp.dll`, or in single-player, the overlay works without these.
- **SA-MP's chat and dialogs may draw over our menu.** SA-MP draws its UI from its device proxy's `Present`, after our hook. Accepted, since the two are rarely open together.

Untouched: `shared/` (wire protocol v2), `ts3plugin/` sources, `src/teamspeak.*`, `src/roster.*`, `src/notify.*`, `src/config.*`, `src/hud.*`, `src/menu.*`, `src/colorpicker.*`, `src/icons.*`, `src/ui_files.*`, `src/log.*`, `assets/ui/*`, both test files. `src/update.cpp` changes only its asset name and messages.

This spec supersedes the window/DirectComposition/DX11/raw-input/foreground-gating parts of `2026-09-21-rmlui-conversion-design.md` and the remaining FiveM-specific parts of the two earlier specs.

## Design

### Shipped artifacts
- `YapNotifierSA.asi`: **x86**, loaded by Silent's ASI Loader or Ultimate ASI Loader from the GTA SA root, `scripts\`, or `scripts\gta_sa\`. It writes `YapNotifier.ini`, `YapNotifier.log` and `YapNotifier.profiles\` next to itself. Requires `gta_sa.exe` 1.0 US, which SA-MP requires anyway.
- `YapNotifier.ts3_plugin`: x64, unchanged apart from its description string.

### Threads
- **Init thread** (`src/main.cpp`, spawned from `DllMain`): unchanged role. It loads the INI, starts the listener, runs the updater, and polls the hotkeys (`GetAsyncKeyState`, only while this process is in the foreground). New: a hotkey edge is ignored while SA-MP reports typing. The game thread reads `samp::typing()` each frame and publishes it as `overlay::Hotkeys::blocked`, so the init thread never touches SA-MP memory. The menu toggle only sets an atomic flag; it no longer posts a window message.
- **Game thread** (new home for all UI work): our frame hook (below) runs on GTA's main thread inside BeginScene/EndScene. The first call initialises RmlUi. After that, each call runs the existing per-frame sequence: apply a pending menu/HUD toggle, drain `ts::drain_events` into `hud::feed`, `hud::sync` + `menu::sync`, `Context::Update`, then render both contexts through `render_d3d9`. The old UI thread, window, message loop and `Sleep(16)` pacing are deleted; we run at the game's frame rate.
- **Listener thread** (`src/teamspeak.cpp`): unchanged.

### `src/game.cpp` (new): everything that knows `gta_sa.exe` addresses
- **Frame hook, installed in `DllMain`.** ASI loaders run us before `WinMain` (Silent's loader hooks the CRT's `GetStartupInfo` import), so the patch is in place before the first frame. It is a `VirtualProtect` + 5-byte write, with no library loads under the loader lock. The hook site is the `E8` call to `CFont::DrawFonts` at **0x53EBB1** in `Idle`. It is the last draw of the frame, after the pause menu, `DoFade` and `CHud::DrawAfterFade`, so it also runs while the game is paused. Before patching:
  - check that this is 1.0 US (the dword at 0x401000 is `0x53EC8B55` for the compact exe or `0x16197BE9` for hoodlum; plugin-sdk `GameVersion.cpp`);
  - check that the byte at 0x53EBB1 is `E8` and that the call target is executable memory (it may be inside another mod that chained the call first);
  - store the **current** target (another mod may already have chained this call) and call it first from our hook, then draw.
  - If either check fails, log it and stay dormant.

  The pre-game main menu goes through `FrontendIdle` and is not hooked. SA-MP never shows it.
- **Device:** read `*(IDirect3DDevice9**)0xC97C28` (`_RwD3DDevice`) each frame and draw through whatever is there. After SA-MP's init that is SA-MP's forwarding proxy, which is fine.
- **Window:** the HWND is `*(HWND*)0xC97C1C`.
- **WndProc subclass, installed from the frame hook.** We install once, after GTA's `MainWndProc` (**0x747EB0**) has been replaced as the window procedure, so we sit above SA-MP's subclass and see every message first. Conditions:
  - if the window procedure is no longer 0x747EB0, install now;
  - else if `samp.dll` is not loaded, install after 3 s (in case it is injected late);
  - else, after 10 s of frames, install anyway.

  GTA's window is ANSI, so we read and set the procedure with the `A` functions and chain with `CallWindowProcA` (keeps SA-MP's chat input seeing the messages it expects). We never uninstall.
- **Camera/controls lock without SA-MP** (`lock_input(bool)`), copied from SA-MP's own `CGame::ToggleKeyInputsDisabled` (dashr9230/SA-MP `saco/game/game.cpp` L177–330):
  - NOP 5 bytes at 0x541DF5 (`CPad::UpdatePads` → `AffectPadFromKeyBoard`);
  - NOP 5 bytes at 0x53F417 (`CPad::UpdateMouse` → `GetMouseState`) and write `33 C0 0F 84` at 0x53F41F;
  - zero 0xB73424 and 0xB73428 and call `CPad::ClearMouseHistory` (0x541BD0);
  - write `C3` at 0x6194A0 (`RsMouseSetPos`) so the game stops re-centring the cursor.

  Before patching, each site is checked against its known original bytes (`E8 46 F3 FE FF` at 0x541DF5, `E8 B4 7A 20 00` at 0x53F417, `85 C0 0F 8C` at 0x53F41F, `E9` at 0x6194A0; decomp `game.cpp` L218–251). Unlocking writes the originals back. On any mismatch we skip the lock and log it: the menu still works, the camera just isn't frozen.
- **Cursor without SA-MP:** GTA keeps the cursor hidden (`ShowCursor(FALSE)` on `WM_SETCURSOR` and activation). Opening the menu calls `ShowCursor(TRUE)` until the display count is non-negative and remembers how many calls that took; closing undoes exactly that many. While the menu is open, the subclass answers `WM_SETCURSOR` with TRUE so GTA never sees it, and RmlUi's cursor shapes are set with `SetCursor` (our system-interface override; the stock one also writes the game's window-class cursor, which we don't want).

### `src/samp.cpp` (new): SA-MP 0.3.DL R1 integration
- **Detection:** look `samp.dll` up lazily on the game thread with `GetModuleHandleW`, because it may load after us. It is 0.3.DL R1 exactly when the PE `OptionalHeader.AddressOfEntryPoint == 0xFDB60`. Any other value turns the module into a no-op, and we log the entry point once so a user's report identifies their version.
- `bool available()`: DL R1 is loaded and `*(CGame**)(base + 0x2ACA3C)` is non-null.
- `void set_cursor(bool on)`: calls `CGame::SetCursorMode` (thiscall at `base + 0xA0530`, `(int mode, BOOL immediatelyHideCursor)`) with `LOCKCAMANDCONTROL = 2` to open and `CURSOR_NONE = 0` to close. This replaces `game::lock_input` and the `WM_SETCURSOR` handling whenever `available()`. We never patch the pad bytes ourselves while SA-MP is present, because SA-MP rewrites them on every mode change.
- `bool typing()`: reads chat input open (`*(CInput**)(base + 0x2ACA14)`, field `+0x14E0`) **or** dialog open (`*(CDialog**)(base + 0x2AC9E0)`, field `+0x28`). Null pointers read as false.
- Every read is wrapped in SEH. A fault logs once and disables the SA-MP module for the session.

### Input routing and the menu (`src/overlay.cpp`, rewritten)
- **Opening the menu** (INSERT, applied at the start of a frame on the game thread):
  - `samp::set_cursor(true)` if `samp::available()`, else `game::lock_input(true)` plus the `WM_SETCURSOR` arrow;
  - `menu::show(true)` and `hud::edit(&cfg)`.

  Closing reverses each step. Closing from the window's `[x]` is detected in `menu::sync` as today.
- **The subclass's message routing** is today's [overlay.cpp:303-321](../../../src/overlay.cpp) logic moved over unchanged:
  - Keys-tab VK capture on key-up;
  - menu context first, then mouse presses the menu left alone plus every move/release to the HUD context (edit-mode drags).

  Mouse coordinates are rescaled from the client area to the back buffer when the two differ (windowed-mode mods that scale). While the menu is open, every keyboard and mouse message (`WM_KEYFIRST..WM_KEYLAST`, which includes `WM_CHAR`, and `WM_MOUSEFIRST..WM_MOUSELAST`) is consumed after RmlUi sees it, so typing in a field never reaches SA-MP's chat or the game. While it is closed, every message passes straight to the previous procedure.
- **Deleted:** raw-input suspend/restore, `ClipCursor`, `SetForegroundWindow` hand-offs, `track_game_window`, foreground show/hide, the `WM_YAP_*` messages.

### `src/render_d3d9.cpp` (new): RmlUi 6.3 `RenderInterface` on D3D9 fixed-function
- **Vertex format:** `D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1`. `CompileGeometry` copies the RmlUi vertices into CPU memory with the colour swizzled RGBA → ARGB `D3DCOLOR` (RmlUi already supplies premultiplied colour), plus 32-bit indices. `RenderGeometry` sets the world matrix to (translation × current transform) and calls `DrawIndexedPrimitiveUP` with `D3DFMT_INDEX32`. `ReleaseGeometry` frees the CPU copy.
- **Textures:** `GenerateTexture` creates an `A8R8G8B8` texture in `D3DPOOL_MANAGED` from RmlUi's premultiplied RGBA, swizzled to BGRA. Fonts and SVG icons arrive this way. `LoadTexture` logs and returns 0, since no document loads image files. `ReleaseTexture` releases the texture.
- **Scissor:** `EnableScissorRegion` / `SetScissorRegion` map to `D3DRS_SCISSORTESTENABLE` + `SetScissorRect`.
- **Transforms:** `SetTransform` stores the matrix (null means identity). The projection is an orthographic pixel-space matrix with D3D9's −0.5 half-pixel offset.
- **Clip mask** (rounded corners with `overflow: hidden`): `EnableClipMask` / `RenderToClipMask` with `Set`, `SetInverse` and `Intersect` implemented as stencil ops, following RmlUi's GL3 backend. We clear the stencil once at the start of our pass; the game has finished with it by then. Each frame we check `GetDepthStencilSurface` for a stencil-bearing format (D24S8, D24X4S4, D15S1). Without one, the clip mask is a no-op and clipping falls back to the scissor rectangle.
- **Layers, filters, shaders:** not implemented (RmlUi's default no-ops). Current documents use none. A future `box-shadow` or `filter` would render without its effect.
- **State isolation, every frame:**
  1. `CreateStateBlock(D3DSBT_ALL)` + `Capture`, and save World/View/Projection by hand (state blocks don't reliably include them).
  2. Set our pipeline: null vertex and pixel shaders, our FVF, no z-test or z-write, `CULL_NONE`, no lighting or fog, alpha blend `ONE / INVSRCALPHA`, stage 0 `MODULATE` texture × diffuse for colour and alpha, linear clamp sampling, scissor and stencil off by default.
  3. Draw.
  4. Restore: `Apply` + `Release` the state block, then the three transforms.

  This matches what `imgui_impl_dx9` does inside gta-reversed's `Idle`. With the device restored exactly, RenderWare's render-state cache and its cached FVF/shader/index-buffer pointers (0x8E2440–0x8E2450) stay truthful, and no RW cache invalidation is needed.
- **Device reset / resolution change:** we hold no `D3DPOOL_DEFAULT` resources (managed textures, `UP` draws, per-frame state block), so we need no lost/reset hooks. Each frame we read `GetViewport`; if the size changed, we call `SetDimensions` on both contexts and rebuild the projection.

### Fault policy
The frame-hook body runs through an `__try` wrapper, as `create_device` does today. On an SEH fault we log it, release the input lock or SA-MP cursor, mark the overlay dead, and from then on the hook only calls the original target. RmlUi initialisation failures (fonts, documents) go dormant the same way. The game is never taken down.

### Build (`CMakeLists.txt`, `YapNotifier.rc`)
- The top-level tree requires **x86** (`CMAKE_SIZEOF_VOID_P EQUAL 4`; error text tells you to use `-A Win32`). It builds RmlUi, FreeType, LunaSVG, `YapNotifierSA` (`OUTPUT_NAME YapNotifierSA`, suffix `.asi`), `test_parser` and `test_ui`, all x86.
- `rmlui_backend` keeps only `RmlUi_Platform_Win32.cpp` (system interface, `WindowProcedure`, IME). The DX11 renderer source goes, and the `d3d11`, `dxgi`, `d3dcompiler`, `dcomp` and `dwmapi` links are replaced by `d3d9` (for the types; the device comes from the game).
- **TS3 plugin:** its target and packaging move into `ts3plugin/CMakeLists.txt`, a standalone project. The top level builds it with `ExternalProject_Add(... CMAKE_GENERATOR_PLATFORM x64)` and copies `YapNotifier.ts3_plugin` to `build/`, so the documented three commands still produce both artifacts. It fetches the TS3 SDK itself.
- `YAPNOTIFIER_DEPLOY_DIR` now means the GTA SA folder (or its `scripts\`).
- `/W4 /permissive-` stays warning-free. New 32-bit narrowing warnings in our sources get fixed, not suppressed.
- `YapNotifier.rc` drops every `FX_ASI_BUILD` line and keeps `YAP_FONT`, `YAP_CHANGELOG`, the `UI_*` RCDATA and version info. FileDescription becomes "TeamSpeak 3 overlay for GTA San Andreas / SA-MP". The PDB stays enabled for crash offsets.

### Updater (`src/update.cpp`)
The asset name becomes **`YapNotifierSA.asi`**. Existing FiveM installs look for an asset named exactly `YapNotifier.asi`; finding none, they log and stay on 0.4.1, so they never download an x86 GTA SA binary. The swap mechanics (rename running file to `.old`, digest check) are unchanged. User-facing text becomes "restart GTA SA to apply".

### Release (0.5.0; shipped by the user as 1.0.0 under the shadyamr/yapnotifier repo)
- `YAP_VERSION` and `YAP_VERSION_NUM` become 0.5.0; so does `ts3plugin/package.ini` `Version`, and its description says SA-MP.
- CI (`.github/workflows/build.yml`): `-A Win32`, collect `build/Release/YapNotifierSA.asi` + `.pdb` + `build/YapNotifier.ts3_plugin`. The version and CHANGELOG checks stay.
- `CHANGELOG.md` `## 0.5.0`: now a GTA SA / SA-MP overlay; FiveM users stay on 0.4.1; install instructions pointer.

### Docs
- `README.md`: install (ASI loader required, where to put the `.asi`, TS3 plugin unchanged), SA-MP 0.3.DL R1 integration vs. other versions, the dialogs-over-menu limitation.
- `CLAUDE.md`: what-this-is, build commands, threads, hooks, renderer, the address table's location (top of `game.cpp` / `samp.cpp`, each with its source).
- The three older specs get a one-line note at the top pointing here for everything platform-specific.

## Address reference

All addresses are for `gta_sa.exe` 1.0 US or are RVAs into `samp.dll` 0.3.DL R1. The pinned sources are plugin-sdk @15f15b60 (PSDK), gta-reversed @f270ecea (GR), BlastHackNet/SAMP-API @6d4db99a (SAMP-API, `0.3.DL-1`), the dashr9230/SA-MP 0.3.7 R5 decompilation @cd3ec8b7 (decomp), and RinatNamazov/SampX @76f91e00. Confidence: H = read in source; M = one source, or offsets computed from struct layout. Every M item is checked live in Task 0.

| Name | Value | Source | Conf |
|---|---|---|---|
| `CFont::DrawFonts` call in `Idle` (frame hook) | 0x53EBB1 | PSDK `shared/Events.h` L360 (`drawFontsEvent`) | M, confirmed in-game (the hook runs) |
| `_RwD3DDevice` | 0xC97C28 | PSDK `common.cpp` L602; GR `RenderWare.h` L30 | H |
| Game HWND copy | 0xC97C1C | GR `WindowedMode.cpp` L107 | H |
| `MainWndProc` | 0x747EB0 | GR `WndProc.cpp` L27, L293 | H |
| `AffectPadFromKeyBoard` call (NOP) | 0x541DF5 | decomp `game.cpp` L206–300 | H |
| `GetMouseState` call (NOP) / branch patch | 0x53F417 / 0x53F41F | decomp `game.cpp` | H |
| Mouse state words to zero | 0xB73424, 0xB73428 | decomp `game.cpp` | H |
| `CPad::ClearMouseHistory` | 0x541BD0 | decomp `game.cpp` | H |
| `RsMouseSetPos` (`C3`) | 0x6194A0 | decomp `game.cpp` | H |
| samp DL R1 entry-point RVA | 0xFDB60 | SampX `SampVersions.cpp` L37 | H |
| samp `RefGame` | +0x2ACA3C | SAMP-API `CGame.cpp` L22–24 | H |
| samp `CGame::SetCursorMode` | +0xA0530 | SAMP-API `CGame.cpp` L38–39 | H |
| samp `RefInputBox` / `m_bEnabled` | +0x2ACA14 / +0x14E0 | SAMP-API `CInput.cpp` L14–15, `CInput.h` L24–31 | H / confirmed in-game (typing gate works) |
| samp `RefDialog` / `m_bIsActive` | +0x2AC9E0 / +0x28 | SAMP-API `CDialog.cpp` L14–15, `CDialog.h` L35 | H / confirmed in-game (typing gate works) |

## Implementation order

0. **Live verification (gate).** Using Cheat Engine on `gta_sa.exe` 1.0 US running SA-MP 0.3.DL R1, confirm:
   - the call target at 0x53EBB1 is `CFont::DrawFonts`;
   - the original bytes at 0x541DF5 (`E8 46 F3 FE FF`);
   - `+0x14E0` toggles when chat input opens and `+0x28` toggles when a dialog opens;
   - DL's cursor mode 2 freezes the camera.

   Correct the table above before writing code that depends on it. If 0x53EBB1 turns out wrong, fall back to `drawHudEvent` (0x53E4FF) and accept that the overlay is hidden in the pause menu.
1. **Build switch.** x86 top level, the TS3 ExternalProject, `rmlui_backend` without DX11, `.rc` cleanup. `test_parser` and `test_ui` pass as x86. The old `overlay.cpp` is stubbed out so the `.asi` links.
2. **Feasibility gate.** `game.cpp` frame hook + `render_d3d9.cpp` + a 3-line `hud.rml` (text + one SVG + a rounded `overflow: hidden` box) rendered in-game in both fullscreen and windowed mode, with alt-tab and a resolution change. If state restoration visibly corrupts the game's rendering, stop and re-plan.
3. **Full overlay.** Rewrite `overlay.cpp` on the game thread with the real HUD and menu documents, plus the WndProc subclass and input routing.
4. **Input lock.** `game::lock_input` + `WM_SETCURSOR` (single-player), then `samp.cpp` (DL R1 cursor and typing).
5. **Updater rename, version bump, CI, CHANGELOG, README, CLAUDE.md, spec notes.**

## Verification

- **Automated** (`ctest --test-dir build -C Release`):
  - `test_parser` and `test_ui` unchanged, now x86.
  - `test_parser` gains pure-helper cases:
    - RGBA → ARGB `D3DCOLOR` swizzle;
    - the orthographic half-pixel projection (corners map to clip-space ±1 within 1e-5);
    - the 1.0 US fingerprint (compact and hoodlum accepted, anything else rejected);
    - samp version detection from a synthetic PE header (0xFDB60 → DL R1; 0xCBC90 → unsupported);
    - the call-site decoder (`E8` decoded forwards and backwards; other opcodes rejected);
    - the updater's asset lookup (finds `YapNotifierSA.asi`, ignores `YapNotifier.asi`).
- **Manual in-game checklist** (the overlay and hooks can't be automated):
  - exclusive fullscreen and windowed/borderless: the HUD draws, text is crisp, icons are tinted;
  - alt-tab out and back; change resolution in GTA's options: no crash, the HUD re-anchors;
  - GTA pause menu: the overlay still draws, the game's rendering is unaffected (no stray blend or fog state);
  - SA-MP 0.3.DL R1: INSERT opens the menu with SA-MP's cursor, the camera is frozen, sliders and colour pickers work, edit-mode drag works, typing in a menu field doesn't open chat, closing restores control;
  - SA-MP 0.3.DL R1: with chat input or a dialog open, INSERT and HOME do nothing;
  - another SA-MP version (e.g. 0.3.7 R1) and single-player: the menu opens with the arrow cursor and a frozen camera via the `gta_sa.exe` patches; the log names the unsupported samp entry point;
  - HOME hides the HUD; Demo mode works; Save/Reload/Profiles behave as before;
  - with ReShade or ENB installed: the overlay still draws and the game is unaffected;
  - updater: a FiveM 0.4.1 install pointed at a release containing only `YapNotifierSA.asi` logs "no asset" and installs nothing.
