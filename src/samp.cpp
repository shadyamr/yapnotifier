#include "samp.h"

#include <Windows.h>

#include <cstddef>
#include <cstring>

#include "log.h"
#include "patch.h"

namespace yap::samp {
namespace {
// samp.dll 0.3.DL R1, RVAs from BlastHackNet/SAMP-API @6d4db99a (include/sampapi/0.3.DL-1), entry
// point from RinatNamazov/SampX SampVersions.cpp. Field offsets confirmed in-game on 0.3.DL R1
// by the typing gate (chat/dialog block the hotkeys); not separately checked in Cheat Engine.
constexpr uint32_t kEntryDL_R1 = 0xFDB60;
constexpr uintptr_t kRefGame = 0x2ACA3C;       // CGame*
constexpr uintptr_t kSetCursorMode = 0xA0530;  // void CGame::SetCursorMode(int mode, BOOL bImmediatelyHideCursor)
constexpr uintptr_t kRefInput = 0x2ACA14;      // CInput* (chat input box)
constexpr uintptr_t kInputEnabled = 0x14E0;    // CInput::m_bEnabled (BOOL)
constexpr uintptr_t kRefDialog = 0x2AC9E0;     // CDialog*
constexpr uintptr_t kDialogActive = 0x28;      // CDialog::m_bIsActive (BOOL)
constexpr int kCursorNone = 0, kCursorLockCamAndControl = 2;

using SetCursorModeFn = void(__thiscall*)(void* game, int mode, BOOL hide_immediately);

HMODULE g_module = nullptr;  // the samp.dll last classified
uintptr_t g_base = 0;        // its base, only when it is 0.3.DL R1
bool g_dead = false;         // a call faulted: integration off for the session

uintptr_t base() {
    if (g_dead) return 0;
    const HMODULE m = GetModuleHandleW(L"samp.dll");  // may load after us: look it up every time
    if (m == g_module) return g_base;
    g_module = m;
    g_base = 0;
    if (!m) return 0;
    uint8_t headers[0x400];
    const uint32_t ep = patch::read(reinterpret_cast<uintptr_t>(m), headers, sizeof headers)
                            ? entry_point_rva(headers, sizeof headers)
                            : 0;
    if (version_from_entry(ep) == Version::DL_R1) {
        g_base = reinterpret_cast<uintptr_t>(m);
        log::info("samp: 0.3.DL R1 detected; using SA-MP's cursor and typing state");
    } else {
        log::info("samp: samp.dll entry point {:#x} is not 0.3.DL R1 (0xfdb60); SA-MP integration off", ep);
    }
    return g_base;
}

uintptr_t game_object() {
    const uintptr_t b = base();
    uintptr_t game = 0;
    return b && patch::read(b + kRefGame, &game, sizeof game) ? game : 0;
}

bool flag(uintptr_t ref, uintptr_t field) {
    const uintptr_t b = base();
    uintptr_t obj = 0;
    BOOL v = FALSE;
    return b && patch::read(b + ref, &obj, sizeof obj) && obj && patch::read(obj + field, &v, sizeof v) && v;
}

bool call_set_cursor_mode(uintptr_t fn, uintptr_t game, int mode, BOOL hide) {
    __try {
        reinterpret_cast<SetCursorModeFn>(fn)(reinterpret_cast<void*>(game), mode, hide);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
}  // namespace

uint32_t entry_point_rva(const uint8_t* image, size_t size) {
    if (!image || size < sizeof(IMAGE_DOS_HEADER)) return 0;
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, image, sizeof dos);
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0) return 0;
    const size_t nt = static_cast<size_t>(dos.e_lfanew);
    const size_t ep_at = nt + 4 + sizeof(IMAGE_FILE_HEADER) + offsetof(IMAGE_OPTIONAL_HEADER32, AddressOfEntryPoint);
    if (ep_at + 4 > size) return 0;
    uint32_t sig = 0, ep = 0;
    std::memcpy(&sig, image + nt, 4);
    if (sig != IMAGE_NT_SIGNATURE) return 0;
    std::memcpy(&ep, image + ep_at, 4);
    return ep;
}

Version version_from_entry(uint32_t rva) {
    if (rva == 0) return Version::None;
    return rva == kEntryDL_R1 ? Version::DL_R1 : Version::Other;
}

bool available() { return game_object() != 0; }

bool set_cursor(bool on) {
    const uintptr_t game = game_object();
    if (!game) return false;
    if (call_set_cursor_mode(g_base + kSetCursorMode, game, on ? kCursorLockCamAndControl : kCursorNone, on ? FALSE : TRUE))
        return true;
    log::error("samp: SetCursorMode faulted; SA-MP integration off for this session");
    g_dead = true;
    g_base = 0;
    return false;
}

bool dialog_open() { return flag(kRefDialog, kDialogActive); }
bool typing() { return flag(kRefInput, kInputEnabled) || dialog_open(); }

}  // namespace yap::samp
