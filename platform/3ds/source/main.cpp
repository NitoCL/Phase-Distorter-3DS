// Phase Distorter 3DS — Nintendo 3DS frontend for the Phase Distorter
// EarthBound / Mother 2 port. Top screen: the game. Bottom screen: party
// status, touch shortcuts and options. Uses libctru only (software blits
// straight into the LCD framebuffers; no GPU shaders needed).
//
// SD card layout (created on first run):
//   sdmc:/3ds/phase-distorter/earthbound.ebpak   asset pack (or mother2.ebpak)
//   sdmc:/3ds/phase-distorter/<your rom>.sfc     imported on first run if no pack
//   sdmc:/3ds/phase-distorter/earthbound.srm     battery save (8 KiB, same as PC)
#include <3ds.h>

#include "audio_3ds.hpp"
#include "bottom_ui.hpp"
#include "canvas.hpp"
#include "eb/asset_store.hpp"
#include "eb/game_session.hpp"
#include "eb/logical_clock_policy.hpp"
#include "eb/snes_bus.hpp"
#include "generated_assets.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <vector>

// The translated game runtime recurses deeper than libctru's 32 KiB default.
extern "C" {
u32 __stacksize__ = 1024 * 1024;
}

namespace {
using namespace pd3ds;

const std::string base_dir = "sdmc:/3ds/phase-distorter/";
#ifndef PD3DS_BUILD
#define PD3DS_BUILD "local"
#endif

// Measures the real ARM11 clock: a register-only loop of known cycle count
// timed against the fixed 268 MHz system tick. A New 3DS without the 804 MHz
// exheader flag (or speedup) shows "268 MHz" here.
int measured_cpu_mhz() {
    constexpr u32 iterations = 200'000;
    u32 counter = iterations, sink = 0;
    const u64 start = svcGetSystemTick();
    // 32 dependent single-cycle ADDs per iteration dominate the loop, so the
    // cycle count is ~34 per iteration whatever the branch costs.
    asm volatile("1:\n\t.rept 32\n\tadd %1, %1, #1\n\t.endr\n\tsubs %0, %0, #1\n\tbne 1b"
                 : "+r"(counter), "+r"(sink)
                 :
                 : "cc");
    const u64 ticks = svcGetSystemTick() - start;
    const double cycles = 34.0 * iterations;
    const double mhz = cycles / (double(ticks) / SYSCLOCK_ARM11) / 1e6;
    return mhz > 500 ? 804 : 268;
}
constexpr double snes_frame_rate = 60.0988;
constexpr unsigned save_check_interval = 60; // Frames between SRAM comparisons.

bool file_exists(const std::string &path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

std::optional<std::vector<std::uint8_t>> read_file(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return std::nullopt;
    std::vector<std::uint8_t> bytes;
    std::uint8_t chunk[16384];
    for (std::size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) bytes.insert(bytes.end(), chunk, chunk + n);
    std::fclose(f);
    return bytes;
}

// Write-then-rename so a power loss never leaves a truncated save.
bool write_file_atomic(const std::string &path, std::span<const std::uint8_t> bytes) {
    const std::string temporary = path + ".tmp";
    FILE *f = std::fopen(temporary.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (std::fclose(f) != 0 || !ok) {
        std::remove(temporary.c_str());
        return false;
    }
    std::remove(path.c_str());
    return std::rename(temporary.c_str(), path.c_str()) == 0;
}

const char *pack_name(eb::GameVersion version) {
    return version == eb::GameVersion::JP ? "mother2" : "earthbound";
}

// ---------------------------------------------------------------- screens --

Canvas top_canvas() {
    return Canvas(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, nullptr, nullptr), top_width);
}
Canvas bottom_canvas() {
    return Canvas(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, nullptr, nullptr), bottom_width);
}

void present() {
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

// Full-screen message on the bottom screen; top screen black.
void show_message(const std::vector<std::string> &lines, Color accent = 0xd7c96b) {
    for (int pass = 0; pass < 2; ++pass) { // Both buffers of the top screen.
        auto top = top_canvas();
        top.clear(0x000000);
        auto c = bottom_canvas();
        c.clear(0x14121c);
        c.fill({0, 0, bottom_width, 24}, 0x2a2540);
        c.fill({0, 24, bottom_width, 1}, accent);
        c.text(8, 7, "Phase Distorter 3DS", 0xf2f0ff);
        int y = 40;
        for (const auto &line : lines) {
            c.text(8, y, line, 0xf2f0ff);
            y += 15;
        }
        present();
    }
}

void wait_for_start_then_exit(const std::vector<std::string> &lines) {
    auto all = lines;
    all.push_back("");
    all.push_back("Presiona START para salir.");
    show_message(all, 0xe8584f);
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
}

// Split a long error so it fits the 320 px screen (52 columns of 6 px).
std::vector<std::string> wrap(const std::string &text, std::size_t columns = 50) {
    std::vector<std::string> lines;
    std::string line;
    for (char ch : text) {
        if (ch == '\n' || line.size() >= columns) {
            lines.push_back(line);
            line.clear();
            if (ch == '\n') continue;
        }
        line += ch;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// ---------------------------------------------------------------- assets ---

std::vector<std::string> rom_candidates() {
    std::vector<std::string> roms;
    if (DIR *dir = opendir(base_dir.c_str())) {
        while (dirent *entry = readdir(dir)) {
            std::string name = entry->d_name;
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            if (lower.size() > 4 && (lower.ends_with(".sfc") || lower.ends_with(".smc")))
                roms.push_back(base_dir + name);
        }
        closedir(dir);
    }
    std::sort(roms.begin(), roms.end());
    return roms;
}

// Returns a loaded pack, importing the player's own ROM on first run.
std::optional<eb::GameAssets> obtain_assets(std::string &error) {
    for (auto version : {eb::GameVersion::US, eb::GameVersion::JP}) {
        const auto pack = base_dir + pack_name(version) + ".ebpak";
        if (!file_exists(pack)) continue;
        show_message({"Cargando " + std::string(pack_name(version)) + ".ebpak..."});
        try {
            return eb::load_game_assets(pack, eb::asset_profiles());
        } catch (const std::exception &e) {
            error += std::string(pack_name(version)) + ".ebpak: " + e.what() + "\n";
        }
    }
    const auto profiles = eb::asset_profiles(); // Static table; outlives the loop.
    for (const auto &rom : rom_candidates()) {
        try {
            const eb::AssetProfile profile = eb::identify_rom(rom, profiles); // Small copy.
            const auto pack = base_dir + pack_name(profile.version) + ".ebpak";
            show_message({"Importando tu ROM (solo la primera vez):", rom.substr(base_dir.size()), "",
                          "Esto puede tardar un poco."});
            eb::import_assets(rom, pack, profile.layout);
            return eb::load_game_assets(pack, eb::asset_profiles());
        } catch (const std::exception &e) {
            error += rom.substr(base_dir.size()) + ": " + e.what() + "\n";
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------- input ----

std::uint16_t snes_buttons(u32 held) {
    struct Pair {
        u32 key;
        std::uint16_t bit;
    };
    // KEY_UP etc. already combine the D-pad and the Circle Pad.
    static constexpr Pair map[] = {{KEY_A, snes::A},         {KEY_B, snes::B},          {KEY_X, snes::X},
                                   {KEY_Y, snes::Y},         {KEY_L, snes::L},          {KEY_R, snes::R},
                                   {KEY_START, snes::Start}, {KEY_SELECT, snes::Select}, {KEY_UP, snes::Up},
                                   {KEY_DOWN, snes::Down},   {KEY_LEFT, snes::Left},    {KEY_RIGHT, snes::Right}};
    std::uint16_t bits = 0;
    for (const auto &[key, bit] : map)
        if (held & key) bits |= bit;
    // The SNES pad cannot report opposite directions at once.
    if ((bits & snes::Up) && (bits & snes::Down)) bits &= ~(snes::Up | snes::Down);
    if ((bits & snes::Left) && (bits & snes::Right)) bits &= ~(snes::Left | snes::Right);
    return bits;
}

// ---------------------------------------------------------------- game -----

int run(const eb::GameAssets &program) {
    const auto version = program.version;
    eb::GameSession session(program.image, version, /*enhanced_gameplay_timing=*/true);
    session.set_logical_clock_policy(eb::LogicalClockPolicy::ActorFrames);
    session.enable_native_sprite_runtime();
    // Catch the APU up only at port accesses and frame ends: same output,
    // without dispatching to it after every emulated CPU instruction.
    session.set_lazy_audio_sync(true);
    // Per-frame cost breakdown shown on the bottom screen.
    eb::frame_profile::clock = [] { return std::uint64_t(svcGetSystemTick()); };

    const std::string save_path = base_dir + pack_name(version) + ".srm";
    UiState ui_state;
    ui_state.version = version;
    ui_state.build_info = std::to_string(measured_cpu_mhz()) + " MHz  " PD3DS_BUILD;
    unsigned notice_frames = 0;
    if (auto save = read_file(save_path)) {
        if (save->size() == session.save_memory().size()) {
            std::copy(save->begin(), save->end(), session.save_memory().begin());
        } else {
            ui_state.notice = "SRAM de tamaño inesperado: no se cargó";
            notice_frames = 600;
        }
    }
    std::vector<std::uint8_t> saved(session.save_memory().begin(), session.save_memory().end());
    const auto flush_save = [&] {
        const auto live = session.save_memory();
        if (std::equal(live.begin(), live.end(), saved.begin())) return;
        if (write_file_atomic(save_path, live)) {
            saved.assign(live.begin(), live.end());
            ui_state.notice = "Partida guardada en la SD";
        } else {
            ui_state.notice = "No se pudo escribir la SRAM en la SD";
        }
        notice_frames = 180;
    };

    bool widescreen = false;
    session.configure_presentation(256, false, false);

    Audio3ds audio;
    ui_state.audio_available = audio.available();
    BottomUi ui;
    // The bottom screen is double-buffered: a changed UI is drawn into the
    // hidden buffer on two consecutive frames (once per buffer), so the LCD
    // never scans a half-cleared screen.
    std::optional<UiState> drawn_ui;
    unsigned bottom_pending = 2;

    unsigned clear_top = 2;    // Letterbox clears pending (one per buffer).
    unsigned last_width = 0;
    u64 window_start = osGetTime();
    unsigned window_frames = 0;
    u64 window_core_ticks = 0;
    u64 window_present_ticks = 0;
    std::uint64_t frame_counter = 0;
    bool touching = false;

    while (aptMainLoop()) {
        hidScanInput();
        const u32 held = hidKeysHeld();
        touchPosition touch{};
        const bool touch_now = held & KEY_TOUCH;
        if (touch_now) hidTouchRead(&touch);

        if (touch_now && !touching) {
            drawn_ui.reset(); // Taps may open/close the options overlay.
            switch (ui.tap(touch.px, touch.py)) {
            case UiAction::ToggleWidescreen:
                widescreen = !widescreen;
                session.configure_presentation(widescreen ? top_width : 256, false, false);
                clear_top = 2;
                break;
            case UiAction::TogglePerformance:
                ui_state.show_performance = !ui_state.show_performance;
                break;
            case UiAction::Exit:
                flush_save();
                return 0;
            case UiAction::None:
                break;
            }
        }
        touching = touch_now;

        const std::uint16_t buttons = snes_buttons(held) | ui.held_buttons(touch_now, touch.px, touch.py);

        const u64 core_start = svcGetSystemTick();
        session.advance_frame(buttons);
        window_core_ticks += svcGetSystemTick() - core_start;
        const auto samples = session.take_audio_samples();
        audio.queue(samples);

        // Top screen: the game, letterboxed into 400x240.
        const u64 present_start = svcGetSystemTick();
        auto top = top_canvas();
        const auto frame = session.presentation_frame();
        const unsigned width = frame.width ? frame.width : 256;
        const int height = int(frame.pixels.size() / width);
        if (width != last_width) {
            last_width = width;
            clear_top = 2;
        }
        if (clear_top) {
            top.clear(0x000000);
            --clear_top;
        }
        top.blit_centered(frame.pixels, int(width), height);
        window_present_ticks += svcGetSystemTick() - present_start;

        // Bottom screen: redraw only when something visible changed.
        ++frame_counter;
        if (frame_counter % save_check_interval == 0) flush_save();
        if (notice_frames && --notice_frames == 0) ui_state.notice.clear();
        if (frame_counter % 10 == 0) ui_state.party = session.party_status();
        ui_state.widescreen = widescreen;
        if (!drawn_ui || !(*drawn_ui == ui_state)) {
            drawn_ui = ui_state;
            bottom_pending = 2;
        }
        if (bottom_pending) {
            auto bottom = bottom_canvas(); // Current back buffer.
            ui.draw(bottom, *drawn_ui);
            --bottom_pending;
        }

        // Performance window (1 s).
        ++window_frames;
        const u64 now = osGetTime();
        if (now - window_start >= 1000) {
            ui_state.fps_tenths = int(window_frames * 10000 / (now - window_start));
            // ticks / frames / (ticks per ms), scaled by 10.
            const auto per_frame = [&](u64 ticks) {
                return int(ticks * 10000 / (u64(SYSCLOCK_ARM11) * window_frames));
            };
            ui_state.frame_ms_tenths = per_frame(window_core_ticks);
            ui_state.render_ms_tenths = per_frame(eb::frame_profile::render_ticks);
            ui_state.audio_ms_tenths = per_frame(eb::frame_profile::audio_ticks);
            ui_state.present_ms_tenths = per_frame(window_present_ticks);
            eb::frame_profile::render_ticks = eb::frame_profile::audio_ticks = 0;
            eb::frame_profile::audio_syncs = 0;
            window_start = now;
            window_frames = 0;
            window_core_ticks = 0;
            window_present_ticks = 0;
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        // Only wait when ahead of the SNES frame budget; a slow frame would
        // otherwise lose a whole extra vblank and halve the rate.
        const double frame_ms = double(svcGetSystemTick() - core_start) * 1000.0 / SYSCLOCK_ARM11;
        if (frame_ms < 1000.0 / snes_frame_rate - 1.0) gspWaitForVBlank();
    }
    flush_save();
    return 0;
}
} // namespace

int main() {
    osSetSpeedupEnable(true); // New 3DS: 804 MHz and L2 cache.
    gfxInitDefault();
    mkdir("sdmc:/3ds", 0777);
    mkdir(base_dir.c_str(), 0777);

    int result = 0;
    try {
        std::string error;
        if (auto program = obtain_assets(error)) {
            show_message({"Iniciando " + program->title + "..."});
            result = run(*program);
        } else {
            std::vector<std::string> lines{"No se encontró el juego.", "",
                                           "Copia a sdmc:/3ds/phase-distorter/ una de:",
                                           " - earthbound.ebpak o mother2.ebpak (de la", "   versión de PC), o",
                                           " - tu propia ROM de EarthBound (US) o", "   Mother 2 (JP) en .sfc/.smc", ""};
            for (auto &line : wrap(error)) lines.push_back(line);
            wait_for_start_then_exit(lines);
        }
    } catch (const std::exception &e) {
        auto lines = wrap(std::string("Error: ") + e.what());
        wait_for_start_then_exit(lines);
        result = 1;
    }
    gfxExit();
    return result;
}
