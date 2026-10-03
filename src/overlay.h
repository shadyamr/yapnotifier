#pragma once
#include <string>

#include "config.h"

// The overlay draws RmlUi inside GTA San Andreas through the game's own D3D9 device, from a hook
// on the game's frame (see game.h); all RmlUi work runs on the game thread.
namespace yap::overlay {

// Call before start().
void set_config(const Config& cfg, std::wstring ini_path);

// Spawns the UI thread (window + device + RmlUi + message loop). Never throws;
// on failure it logs and the overlay stays dormant.
void start();

// Stops the UI thread and tears everything down. Safe if start() failed.
void stop();

// Toggle the interactive config menu (from the hotkey poll on another thread).
void toggle_menu();

// Flip Config::hud_hidden on the UI thread (from the hotkey poll on another thread).
void toggle_hud();

// The live hotkeys (rebindable in the menu's Keys tab) for the poll; 0 = don't act.
struct Hotkeys {
    int menu = 0, hide = 0;
};
Hotkeys hotkeys();

}  // namespace yap::overlay
