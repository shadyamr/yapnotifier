#pragma once
#include <string>

#include "config.h"

// The overlay draws RmlUi inside GTA San Andreas through the game's own D3D9 device, from a hook
// on the game's frame (see game.h); all RmlUi work runs on the game thread. The init thread only
// sets flags and reads the published hotkeys.
namespace yap::overlay {

// Init thread, once, before the first frame can use the config.
void set_config(const Config& cfg, std::wstring ini_path);

// The frame hook (game thread). Initialises RmlUi on first use; logs and goes dormant on failure.
void frame();

// From the hotkey poll (init thread); applied at the start of the next frame.
void toggle_menu();
void toggle_hud();

// The live hotkeys (rebindable in the menu's Keys tab) for the poll; 0 = don't act. `blocked` while
// SA-MP's chat input or a dialog is open, so typing INSERT/HOME into chat doesn't toggle anything.
struct Hotkeys {
    int menu = 0, hide = 0;
    bool blocked = false;
};
Hotkeys hotkeys();

}  // namespace yap::overlay
