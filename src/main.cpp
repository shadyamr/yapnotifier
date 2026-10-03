#include <Windows.h>

#include <filesystem>

#include "config.h"
#include "game.h"
#include "log.h"
#include "overlay.h"
#include "teamspeak.h"
#include "update.h"
#include "version.h"

namespace {
HMODULE g_module = nullptr;

std::filesystem::path module_path() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(g_module, buf, MAX_PATH);
    return buf;
}

DWORD WINAPI init_thread(LPVOID) {
    const auto self = module_path();
    const auto dir = self.parent_path();
    yap::log::open((dir / L"YapNotifier.log").wstring());
    yap::log::info("loading v" YAP_VERSION " (pid {})", GetCurrentProcessId());
    yap::update::cleanup_previous(self);

    const auto ini = (dir / L"YapNotifier.ini").wstring();
    const yap::Config cfg = yap::config::load(ini);
    yap::overlay::set_config(cfg, ini);
    yap::ts::configure(cfg.port);
    if (const uintptr_t t = yap::game::hooked_target())
        yap::log::info("game: frame hook installed (chained to {:#x})", t);
    else
        yap::log::error("game: not gta_sa.exe 1.0 US, or its frame call is not where expected; overlay off");

    yap::ts::start();
    yap::update::check_and_install(self, cfg.auto_update);

    // Poll the menu and hide-HUD hotkeys. GetAsyncKeyState reads global state, so only act on it while
    // the game is in the foreground and SA-MP's chat input / dialogs are closed. The keys come live
    // from the overlay (rebindable in the menu). There is deliberately no eject: the plugin lives
    // for the whole process.
    auto down = [](int vk) { return vk && (GetAsyncKeyState(vk) & 0x8000) != 0; };
    bool menu_was_down = false, hide_was_down = false;
    for (;;) {
        const auto keys = yap::overlay::hotkeys();
        bool menu_down = down(keys.menu);
        bool hide_down = down(keys.hide);
        DWORD fg_pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &fg_pid);
        const bool act = fg_pid == GetCurrentProcessId() && !keys.blocked;
        if (menu_down && !menu_was_down && act) yap::overlay::toggle_menu();
        if (hide_down && !hide_was_down && act) yap::overlay::toggle_hud();
        menu_was_down = menu_down;
        hide_was_down = hide_down;

        Sleep(30);
    }
}
}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        // ASI loaders run us before WinMain, so this lands before the first frame. Only a
        // VirtualProtect + 4-byte write here; everything else waits for the init thread.
        yap::game::install_frame_hook(&yap::overlay::frame);
        if (HANDLE t = CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
