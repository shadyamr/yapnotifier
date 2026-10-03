#include "game.h"

#include "log.h"
#include "patch.h"

namespace yap::game {
namespace {
// gta_sa.exe 1.0 US. PSDK = DK22Pac/plugin-sdk @15f15b60, GR = gta-reversed @f270ecea,
// decomp = dashr9230/SA-MP @cd3ec8b7 (saco/game/game.cpp).
constexpr uintptr_t kVersionProbe = 0x401000;  // PSDK shared/GameVersion.cpp
constexpr uintptr_t kFrameCall = 0x53EBB1;     // Idle: call CFont::DrawFonts (PSDK Events.h L360, drawFontsEvent)
constexpr uintptr_t kDevice = 0xC97C28;        // _RwD3DDevice (PSDK common.cpp L602)
constexpr uintptr_t kHwnd = 0xC97C1C;          // RsGlobal.ps->window copy (GR WindowedMode.cpp L107)

FrameFn g_on_frame = nullptr;
uintptr_t g_original = 0;

void __cdecl frame_hook() {
    reinterpret_cast<void(__cdecl*)()>(g_original)();
    g_on_frame();
}

bool executable(uintptr_t addr) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<const void*>(addr), &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return false;
    return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

constexpr uintptr_t kMainWndProc = 0x747EB0;  // GR source/WndProc.cpp L27
constexpr uintptr_t kClearMouseHistory = 0x541BD0, kUpdatePads = 0x541DD0;  // decomp L167-180
constexpr uintptr_t kMouseX = 0xB73424, kMouseY = 0xB73428;                 // decomp DIResetMouse

// SA-MP's own camera/controls lock (decomp CGame::ToggleKeyInputsDisabled, mode 2, L204-280):
// keyboard off the pad, DirectInput mouse off, no cursor re-centring.
const patch::Site kInputSites[] = {
    {0x541DF5, {0xE8, 0x46, 0xF3, 0xFE, 0xFF}, {0x90, 0x90, 0x90, 0x90, 0x90}},  // CPad::UpdatePads -> AffectPadFromKeyBoard
    {0x53F417, {0xE8, 0xB4, 0x7A, 0x20, 0x00}, {0x90, 0x90, 0x90, 0x90, 0x90}},  // CPad::UpdateMouse -> GetMouseState
    {0x53F41F, {0x85, 0xC0, 0x0F, 0x8C}, {0x33, 0xC0, 0x0F, 0x84}},              // ... and skip using its result
    {0x6194A0, {0xE9}, {0xC3}},                                                  // RsMouseSetPos -> ret
};
bool g_locked = false;

void reset_mouse() {
    *reinterpret_cast<volatile DWORD*>(kMouseX) = 0;
    *reinterpret_cast<volatile DWORD*>(kMouseY) = 0;
    reinterpret_cast<void(__cdecl*)()>(kClearMouseHistory)();
}

MsgFn g_on_message = nullptr;
WNDPROC g_prev_proc = nullptr;
bool g_ansi = false;  // GTA's window is ANSI: keep it ANSI so SA-MP's chat input sees what it expects
uint64_t g_first_try = 0;
uint64_t g_retry_at = 0;  // after a failed subclass attempt

LRESULT CALLBACK subclass_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    LRESULT result = 0;
    if (g_on_message && g_on_message(h, m, w, l, result)) return result;
    return g_ansi ? CallWindowProcA(g_prev_proc, h, m, w, l) : CallWindowProcW(g_prev_proc, h, m, w, l);
}
}  // namespace

bool install_frame_hook(FrameFn on_frame) {
    uint32_t probe = 0;
    if (!patch::read(kVersionProbe, &probe, sizeof probe) || !patch::is_sa_10us(probe)) return false;
    uint8_t bytes[5];
    uintptr_t target = 0;
    if (!patch::read(kFrameCall, bytes, sizeof bytes) || !patch::decode_call(bytes, kFrameCall, target) ||
        !executable(target))
        return false;
    g_on_frame = on_frame;
    g_original = target;
    const int32_t rel = patch::call_rel(kFrameCall, reinterpret_cast<uintptr_t>(&frame_hook));
    if (patch::write(kFrameCall + 1, &rel, sizeof rel)) return true;
    g_original = 0;
    return false;
}

uintptr_t hooked_target() { return g_original; }

IDirect3DDevice9* device() {
    IDirect3DDevice9* d = nullptr;
    return patch::read(kDevice, &d, sizeof d) ? d : nullptr;
}

HWND window() {
    HWND h = nullptr;
    return patch::read(kHwnd, &h, sizeof h) ? h : nullptr;
}

// We want to sit above SA-MP's subclass (installed from its game-loop hook a while after start) so
// we see input first and can keep menu typing away from its chat. So: wait until something has
// replaced GTA's MainWndProc; without samp.dll, give it 3 s in case samp.dll is injected late;
// with samp.dll but no subclass yet, 10 s.
bool subclass_window(MsgFn on_message, uint64_t now_ms, bool samp_loaded) {
    if (g_prev_proc) return true;
    const HWND h = window();
    if (!h) return false;
    if (now_ms < g_retry_at) return false;
    if (!g_first_try) g_first_try = now_ms;
    g_ansi = !IsWindowUnicode(h);
    const LONG_PTR cur = g_ansi ? GetWindowLongPtrA(h, GWLP_WNDPROC) : GetWindowLongPtrW(h, GWLP_WNDPROC);
    const uint64_t waited = now_ms - g_first_try;
    const bool replaced = static_cast<uintptr_t>(cur) != kMainWndProc;
    if (!replaced && waited < (samp_loaded ? 10000u : 3000u)) return false;
    g_on_message = on_message;
    g_prev_proc = reinterpret_cast<WNDPROC>(
        g_ansi ? SetWindowLongPtrA(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&subclass_proc))
               : SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&subclass_proc)));
    if (!g_prev_proc) {
        log::error("game: subclassing the game window failed ({})", GetLastError());
        g_retry_at = now_ms + 60000;  // don't retry every frame
        return false;
    }
    log::info("game: window subclassed{}", replaced ? " above SA-MP" : "");
    return true;
}

bool lock_input(bool on) {
    if (on == g_locked) return true;
    if (on) {
        for (const auto& s : kInputSites) {
            if (!patch::matches(s)) {
                log::error("game: input lock skipped, unexpected bytes at {:#x}", s.addr);
                return false;
            }
        }
    }
    for (const auto& s : kInputSites) patch::apply(s, on);
    g_locked = on;  // before the calls into game code, so a fault there still leaves lock_input(false) effective
    reset_mouse();
    if (on) reinterpret_cast<void(__cdecl*)()>(kUpdatePads)();
    return true;
}

}  // namespace yap::game
