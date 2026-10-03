#include "overlay.h"

#include "colorpicker.h"
#include "game.h"
#include "hud.h"
#include "log.h"
#include "menu.h"
#include "render_d3d9.h"
#include "teamspeak.h"
#include "ui_files.h"
#include "update.h"

#include <Windows.h>

#include <RmlUi/Core.h>
#include <RmlUi_Platform_Win32.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <optional>
#include <vector>

namespace {
using namespace yap;

// Game thread state.
Config g_cfg;
std::wstring g_ini;
hud::State g_hud;
ULONGLONG g_last_frame = 0;
bool g_tried_init = false, g_ready = false;
bool g_menu_open = false;
Rml::Vector2i g_size{0, 0};

std::optional<SystemInterface_Win32> g_system;
std::optional<render::D3D9> g_renderer;
TextInputMethodEditor_Win32 g_ime;
Rml::Context* g_rml = nullptr;       // HUD, dp ratio = cfg.scale
Rml::Context* g_menu_ctx = nullptr;  // menu, dp ratio 1
std::optional<menu::Host> g_menu_host;

// Shared with the init thread.
std::atomic<bool> g_configured{false};
std::atomic<bool> g_dead{false};  // a fault or failed init: the hook only chains from now on
std::atomic<bool> g_want_menu{false}, g_want_hide{false};
// The hotkeys the init thread polls, published from g_cfg every frame; 0 while the Keys tab waits
// for a key so pressing the old hotkey rebinds instead of toggling.
std::atomic<int> g_menu_key{0}, g_hide_key{0};
std::atomic<bool> g_blocked{false};

// `font_file` (relative to the .asi's folder) if set, else Inter from the RCDATA resource in
// YapNotifier.rc. Both register as family "Yap", which the RCSS uses; weight Auto loads every
// named instance of a variable font (Inter: 100..900) so font-weight 500/600 resolve.
void load_font() {
    if (!g_cfg.font_file.empty()) {
        std::filesystem::path p = g_cfg.font_file;
        if (p.is_relative()) p = std::filesystem::path(g_ini).parent_path() / p;
        if (Rml::LoadFontFace(p.string(), "Yap", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto)) return;
        log::error("overlay: could not load font {}, using Inter", p.string());
    }
    auto blob = ui_files::resource(L"YAP_FONT");
    if (blob.empty() || !Rml::LoadFontFace(blob, "Yap", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto))
        log::error("overlay: font resource missing, text will not render");
}

bool init_rml() {
    g_system.emplace();
    if (HWND w = game::window()) g_system->SetWindow(w);  // clipboard
    g_renderer.emplace();
    g_renderer->set_device(game::device());
    Rml::SetSystemInterface(&*g_system);
    Rml::SetRenderInterface(&*g_renderer);
    Rml::SetFileInterface(&ui_files::instance());
    if (!Rml::Initialise()) {
        log::error("overlay: Rml::Initialise failed");
        return false;
    }
    Rml::SetTextInputHandler(&g_ime);
    colorpicker::register_element();
    load_font();
    // Sized properly on the first frame (g_size starts at 0x0).
    g_rml = Rml::CreateContext("yap", {1, 1});
    g_menu_ctx = Rml::CreateContext("menu", {1, 1});
    if (!g_rml || !g_menu_ctx) {
        log::error("overlay: Rml::CreateContext failed");
        return false;
    }
    g_rml->SetDensityIndependentPixelRatio(g_cfg.scale);
    if (!hud::init(*g_rml)) return false;
    g_menu_host.emplace(menu::Host{g_cfg, g_ini, false, g_hud});
    return menu::init(*g_menu_host, *g_menu_ctx);
}

void set_menu(bool open) {
    g_menu_open = open;
    g_menu_host->open = open;
    menu::show(open);
    hud::edit(open ? &g_cfg : nullptr);
}

void sync_hud(float dt_ms, ULONGLONG now) {
    static std::vector<ts::Event> events;
    events.clear();
    // The updater's banner: 20 s after it first appears.
    static ULONGLONG notice_seen = 0;
    auto notice = update::notice();
    if (!notice->empty() && !notice_seen) notice_seen = now;
    const std::string banner = notice_seen && now - notice_seen <= 20000 ? *notice : std::string();

    if (g_cfg.demo) {
        hud::demo_tick(g_hud, g_cfg, now);
        std::vector<ts::Event> dropped;
        ts::drain_events(dropped);  // keep the real queue from piling up meanwhile
        hud::sync(g_hud, g_cfg, hud::demo_snapshot(), dt_ms, now, banner);
        return;
    }
    ts::drain_events(events);
    hud::feed(g_hud, g_cfg, events, now);
    hud::sync(g_hud, g_cfg, *ts::snapshot(), dt_ms, now, banner);
}

void frame_unguarded() {
    if (!g_configured.load(std::memory_order_acquire)) return;
    IDirect3DDevice9* dev = game::device();
    if (!dev) return;
    if (!g_tried_init) {
        g_tried_init = true;
        if (!init_rml()) {
            log::error("overlay: initialisation failed; overlay disabled for this session, game unaffected");
            g_dead = true;
            return;
        }
        g_ready = true;
        log::info("overlay: ready (in-game D3D9, RmlUi {})", Rml::GetVersion());
    }
    if (!g_ready) return;
    const ULONGLONG now = GetTickCount64();
    const float dt_ms = g_last_frame ? static_cast<float>(std::min<ULONGLONG>(now - g_last_frame, 250)) : 16.f;
    g_last_frame = now;

    if (g_want_menu.exchange(false)) set_menu(!g_menu_open);
    if (g_want_hide.exchange(false)) {
        g_cfg.hud_hidden = !g_cfg.hud_hidden;
        log::info("overlay: HUD {}", g_cfg.hud_hidden ? "hidden" : "shown");
    }

    g_renderer->set_device(dev);
    Rml::Vector2i size;
    if (!g_renderer->begin_frame(size)) return;
    if (size != g_size) {
        g_size = size;
        g_rml->SetDimensions(size);
        g_menu_ctx->SetDimensions(size);
    }
    g_rml->SetDensityIndependentPixelRatio(g_cfg.scale);  // every RCSS size is in dp
    const bool capturing = menu::capturing();
    g_menu_key = capturing ? 0 : g_cfg.menu_key;
    g_hide_key = capturing ? 0 : g_cfg.hide_key;
    sync_hud(dt_ms, now);
    if (g_menu_open) {
        menu::sync(*g_menu_host);
        if (!g_menu_host->open) set_menu(false);  // the window's [x]
    }
    g_rml->Update();
    g_menu_ctx->Update();
    if (!g_cfg.hud_hidden) g_rml->Render();  // still synced while hidden, so toasts expire
    g_menu_ctx->Render();                    // on top of the HUD
    g_renderer->end_frame();
}

// Separate from frame() so the SEH frame holds no C++ objects (C2712).
void on_fault() {
    g_dead = true;
    log::error("overlay: fault in the frame; overlay disabled for this session, game unaffected");
    if (g_renderer) g_renderer->end_frame();  // put the device state back if we died mid-draw
}

bool on_fault_guarded() {
    __try {
        on_fault();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
}  // namespace

namespace yap::overlay {

void set_config(const Config& cfg, std::wstring ini_path) {
    g_cfg = cfg;
    g_ini = std::move(ini_path);
    g_menu_key = cfg.menu_key;
    g_hide_key = cfg.hide_key;
    g_configured.store(true, std::memory_order_release);
}

void frame() {
    if (g_dead.load()) return;
    __try {
        frame_unguarded();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        on_fault_guarded();
    }
}

void toggle_menu() { g_want_menu = true; }
void toggle_hud() { g_want_hide = true; }
Hotkeys hotkeys() { return {g_menu_key.load(), g_hide_key.load(), g_blocked.load()}; }

}  // namespace yap::overlay
