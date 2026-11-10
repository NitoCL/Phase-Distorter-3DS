// Host check for the 3DS UI code: renders the bottom screen (party + options)
// and a letterboxed top screen into real 3DS-layout framebuffers, then writes
// them as unrotated PPM images. Also asserts touch mappings.
#include "../source/bottom_ui.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

using namespace pd3ds;

static void write_ppm(const char *path, const std::vector<std::uint8_t> &fb, int width) {
    FILE *f = std::fopen(path, "wb");
    std::fprintf(f, "P6 %d %d 255\n", width, screen_height);
    for (int y = 0; y < screen_height; ++y)
        for (int x = 0; x < width; ++x) {
            const auto *p = &fb[(x * screen_height + (screen_height - 1 - y)) * 3];
            const std::uint8_t rgb[3] = {p[2], p[1], p[0]};
            std::fwrite(rgb, 1, 3, f);
        }
    std::fclose(f);
}

int main(int argc, char **argv) {
    const std::string out = argc > 1 ? argv[1] : ".";
    UiState state;
    state.party[0] = {1, 23, 187, 214, 96, 120, 0};
    state.party[1] = {2, 22, 31, 168, 210, 233, 0};
    state.party[2] = {3, 22, 0, 175, 0, 0, 1};
    state.party[3] = {4, 20, 140, 151, 188, 190, 0};
    state.fps_tenths = 598;
    state.frame_ms_tenths = 142;
    state.notice = "Partida guardada en la SD";

    BottomUi ui;
    std::vector<std::uint8_t> bottom(bottom_width * screen_height * 3);
    Canvas canvas(bottom.data(), bottom_width);
    ui.draw(canvas, state);
    write_ppm((out + "/bottom_party.ppm").c_str(), bottom, bottom_width);

    // Touch shortcuts map to SNES bits only inside their buttons.
    assert(ui.held_buttons(true, 50, 210) == snes::L);
    assert(ui.held_buttons(true, 160, 210) == snes::X);
    assert(ui.held_buttons(false, 50, 210) == 0);
    assert(ui.held_buttons(true, 160, 100) == 0);

    // Options overlay: opening it disables shortcuts; rows return actions.
    assert(ui.tap(260, 210) == UiAction::None && ui.options_open());
    assert(ui.held_buttons(true, 50, 210) == 0);
    assert(ui.tap(150, 60) == UiAction::ToggleWidescreen);
    assert(ui.tap(150, 100) == UiAction::TogglePerformance);
    assert(ui.tap(150, 150) == UiAction::Exit);
    state.widescreen = true;
    state.audio_available = false;
    state.notice.clear();
    ui.draw(canvas, state);
    write_ppm((out + "/bottom_options.ppm").c_str(), bottom, bottom_width);

    // No party yet (title screen).
    ui.tap(260, 210);
    assert(!ui.options_open());
    UiState title;
    ui.draw(canvas, title);
    write_ppm((out + "/bottom_title.ppm").c_str(), bottom, bottom_width);

    // Top screen: a 256x224 test card letterboxed into 400x240, corners marked.
    std::vector<std::uint32_t> frame(256 * 224);
    for (int y = 0; y < 224; ++y)
        for (int x = 0; x < 256; ++x)
            frame[y * 256 + x] = 0xff000000u | (std::uint32_t(x) << 16) | (std::uint32_t(y) << 8) | 0x60;
    frame[0] = 0xffffffff;
    frame[255 + 223 * 256] = 0xffff0000;
    std::vector<std::uint8_t> top(top_width * screen_height * 3);
    Canvas top_canvas(top.data(), top_width);
    top_canvas.clear(0x000000);
    top_canvas.blit_centered(frame, 256, 224);
    // Top-left source pixel must land at (72, 8); bottom-right at (327, 231).
    auto at = [&](int x, int y) {
        const auto *p = &top[(x * screen_height + (screen_height - 1 - y)) * 3];
        return std::uint32_t(p[2]) << 16 | std::uint32_t(p[1]) << 8 | p[0];
    };
    assert(at(72, 8) == 0xffffff);
    assert(at(327, 231) == 0xff0000);
    assert(at(71, 8) == 0 && at(72, 7) == 0 && at(328, 231) == 0 && at(327, 232) == 0);
    write_ppm((out + "/top_letterbox.ppm").c_str(), top, top_width);
    std::puts("preview_ui: all checks passed");
    return 0;
}
