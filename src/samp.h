#pragma once
// SA-MP 0.3.DL R1 integration: its cursor/camera lock for the menu, and "is the player typing"
// (chat input or a dialog open) for the hotkeys. Any other samp.dll, or none, makes every call
// a no-op that returns false. Game thread only.
#include <cstddef>
#include <cstdint>

namespace yap::samp {

enum class Version { None, DL_R1, Other };

// Pure, tested: AddressOfEntryPoint from the first `size` bytes of a PE image, 0 if not one.
uint32_t entry_point_rva(const uint8_t* image, size_t size);
Version version_from_entry(uint32_t rva);

// samp.dll 0.3.DL R1 is loaded and its CGame exists.
bool available();
// CGame::SetCursorMode: on = LOCKCAMANDCONTROL (cursor, camera and player frozen), off = NONE.
// False if unavailable or it faulted (which switches the integration off for the session).
bool set_cursor(bool on);
// A SA-MP dialog is open.
bool dialog_open();
// SA-MP's chat input or a dialog is open.
bool typing();

}  // namespace yap::samp
