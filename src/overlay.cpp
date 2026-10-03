#include "overlay.h"

#include "log.h"

// Placeholder while the overlay moves from its own D3D11 window to in-game D3D9 (SA-MP port, Task 3).
namespace yap::overlay {
void set_config(const Config&, std::wstring) {}
void start() { log::info("overlay: renderer not ported yet; HUD disabled in this build"); }
void stop() {}
void toggle_menu() {}
void toggle_hud() {}
Hotkeys hotkeys() { return {}; }
}  // namespace yap::overlay
