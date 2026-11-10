#pragma once
// Bottom-screen companion UI: party status, touch shortcuts and options.
// Platform-independent; main.cpp feeds it touch input and a Canvas.
#include "canvas.hpp"
#include "eb/game_session.hpp"
#include "eb/game_version.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace pd3ds {

// SNES JOY1 bits, as GameSession::advance_frame expects.
namespace snes {
constexpr std::uint16_t B = 1u << 15, Y = 1u << 14, Select = 1u << 13, Start = 1u << 12;
constexpr std::uint16_t Up = 1u << 11, Down = 1u << 10, Left = 1u << 9, Right = 1u << 8;
constexpr std::uint16_t A = 1u << 7, X = 1u << 6, L = 1u << 5, R = 1u << 4;
} // namespace snes

struct UiState {
    std::array<eb::PartyMemberStatus, 4> party{};
    eb::GameVersion version = eb::GameVersion::US;
    bool widescreen = false;
    bool show_performance = true;
    bool profile_detail = false; // Sampling profile replaces the party panel.
    // Sampling profile, indexed by eb::frame_profile::Phase.
    std::array<std::uint16_t, 9> phase_ms_tenths{};
    std::array<std::uint8_t, 9> phase_percent{};
    int kinstr_per_frame = 0; // Emulated 65816 instructions per frame, thousands.
    int ns_per_instr = 0;     // Mean game-loop time per emulated instruction.
    bool original_timing = true; // Saved setting; takes effect on next start.
    unsigned frame_skip = 1;     // Video frames skipped per rendered frame.
    bool audio_available = true;
    int fps_tenths = 0;     // Measured presentation rate x10.
    int frame_ms_tenths = 0; // Mean core time per frame x10.
    // Breakdown x10 (ms per frame): PPU render, APU+DSP, frame copy to screen.
    // Game logic is frame_ms - render - audio.
    int render_ms_tenths = 0, audio_ms_tenths = 0, present_ms_tenths = 0;
    std::string notice;      // One-line message (save written, errors...).
    std::string build_info;  // e.g. "804 MHz · 750c515": measured clock + commit.
    bool operator==(const UiState &) const = default;
};

enum class UiAction { None, ToggleWidescreen, TogglePerformance, ToggleOriginalTiming, CycleFrameSkip, Exit };

class BottomUi {
  public:
    // Touch shortcuts held this frame, as SNES button bits.
    std::uint16_t held_buttons(bool touching, int x, int y) const;
    // Call on the frame the stylus first touches the screen.
    UiAction tap(int x, int y);
    void draw(Canvas &canvas, const UiState &state) const;
    bool options_open() const { return options_open_; }

    // Layout, exposed for tests.
    static constexpr Rect talk_button{4, 198, 102, 38};
    static constexpr Rect map_button{109, 198, 102, 38};
    static constexpr Rect options_button{214, 198, 102, 38};
    static constexpr std::array<Rect, 5> option_rows{
        {{8, 31, 304, 30}, {8, 64, 304, 30}, {8, 97, 304, 30}, {8, 130, 304, 30}, {8, 163, 304, 30}}};

  private:
    void draw_party(Canvas &canvas, const UiState &state) const;
    void draw_options(Canvas &canvas, const UiState &state) const;
    void draw_profile(Canvas &canvas, const UiState &state) const;
    bool options_open_ = false;
};

} // namespace pd3ds
