#pragma once
// Everything that knows gta_sa.exe 1.0 US addresses. Sources for each address are in game.cpp
// and the spec (docs/superpowers/specs/2026-10-03-samp-port-design.md, "Address reference").
#include <Windows.h>
#include <d3d9.h>

#include <cstdint>

namespace yap::game {

using FrameFn = void (*)();
// DllMain: points the call to CFont::DrawFonts in Idle (the frame's last draw, also while paused)
// at a hook that calls the previous target and then `on_frame`. False if this is not 1.0 US or
// the site is not a call to executable code; nothing is patched then.
bool install_frame_hook(FrameFn on_frame);
// What the hooked call reached before us (CFont::DrawFonts, or another mod's hook); 0 if not hooked.
uintptr_t hooked_target();

IDirect3DDevice9* device();  // the game's device (SA-MP's proxy once it is up); may be null
HWND window();               // the game window; may be null early on

// Window subclass, installed on top of SA-MP's (see game.cpp). Call every frame from the game
// thread; true once installed. `on_message` sees every message first: true = consumed, `result`
// is returned to Windows; false = passed on down the chain.
using MsgFn = bool (*)(HWND, UINT, WPARAM, LPARAM, LRESULT& result);
bool subclass_window(MsgFn on_message, uint64_t now_ms, bool samp_loaded);

// Camera/controls lock without SA-MP, by SA-MP's own gta_sa.exe patches. False (logged) if a
// site doesn't hold the stock bytes; nothing is written then.
bool lock_input(bool on);

}  // namespace yap::game
