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

bool subclass_window(MsgFn, uint64_t, bool) { return false; }  // Task 4
bool lock_input(bool) { return false; }                         // Task 4

}  // namespace yap::game
