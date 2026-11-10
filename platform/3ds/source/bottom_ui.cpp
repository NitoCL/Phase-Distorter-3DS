#include "bottom_ui.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace pd3ds {
namespace {
namespace color {
constexpr Color background = 0x14121c;
constexpr Color header = 0x2a2540;
constexpr Color card = 0x221f33;
constexpr Color card_border = 0x4a4470;
constexpr Color text = 0xf2f0ff;
constexpr Color dim = 0x8a86a8;
constexpr Color bar_empty = 0x0d0c14;
constexpr Color hp = 0xe8584f;
constexpr Color hp_low = 0xffb000;
constexpr Color pp = 0x5b8def;
constexpr Color button = 0x3a3460;
constexpr Color accent = 0xd7c96b;
constexpr Color knocked_out = 0x6b2a2a;
} // namespace color

// Default roster names; players may rename them in-game, so these are labels.
const char *character_name(unsigned id) {
    static constexpr const char *names[] = {"", "Ness", "Paula", "Jeff", "Poo"};
    return id <= 4 ? names[id] : "?";
}

void draw_bar(Canvas &c, int x, int y, int w, unsigned value, unsigned maximum, Color fill) {
    c.fill({x, y, w, 8}, color::bar_empty);
    if (maximum == 0) return;
    const int filled = int((std::min(value, maximum) * unsigned(w - 2)) / maximum);
    c.fill({x + 1, y + 1, filled, 6}, fill);
}

void draw_button(Canvas &c, Rect r, const char *label, const char *hint) {
    c.fill(r, color::button);
    c.frame(r, color::accent);
    c.text_centered({r.x, r.y + 3, r.w, 18}, label, color::text);
    if (hint) c.text_centered({r.x, r.y + 19, r.w, 16}, hint, color::dim);
}
} // namespace

std::uint16_t BottomUi::held_buttons(bool touching, int x, int y) const {
    if (!touching || options_open_) return 0;
    if (talk_button.contains(x, y)) return snes::L;
    if (map_button.contains(x, y)) return snes::X;
    return 0;
}

UiAction BottomUi::tap(int x, int y) {
    if (options_button.contains(x, y)) {
        options_open_ = !options_open_;
        return UiAction::None;
    }
    if (!options_open_) return UiAction::None;
    if (option_rows[0].contains(x, y)) return UiAction::ToggleWidescreen;
    if (option_rows[1].contains(x, y)) return UiAction::TogglePerformance;
    if (option_rows[2].contains(x, y)) return UiAction::ToggleOriginalTiming;
    if (option_rows[3].contains(x, y)) return UiAction::CycleFrameSkip;
    if (option_rows[4].contains(x, y)) return UiAction::Exit;
    return UiAction::None;
}

void BottomUi::draw(Canvas &c, const UiState &state) const {
    c.clear(color::background);
    c.fill({0, 0, bottom_width, 24}, color::header);
    c.fill({0, 24, bottom_width, 1}, color::accent);
    const int title_width =
        c.text(8, 7, state.version == eb::GameVersion::JP ? "Mother 2" : "EarthBound", color::text);
    if (!state.build_info.empty()) c.text(8 + title_width + 8, 7, state.build_info, color::dim);
    if (state.show_performance && state.fps_tenths > 0) {
        char perf[40];
        std::snprintf(perf, sizeof perf, "%d.%d fps  %d.%d ms", state.fps_tenths / 10, state.fps_tenths % 10,
                      state.frame_ms_tenths / 10, state.frame_ms_tenths % 10);
        c.text(bottom_width - 8 - Canvas::text_width(perf), 7, perf, color::dim);
    }

    if (options_open_)
        draw_options(c, state);
    else if (state.show_performance && state.profile_detail)
        draw_profile(c, state);
    else
        draw_party(c, state);

    if (options_open_) {
        // The option rows use this space; messages wait until it closes.
    } else if (!state.notice.empty()) {
        c.text_centered({0, 180, bottom_width, 14}, state.notice, color::accent);
    } else if (state.show_performance && state.fps_tenths > 0 && !options_open_) {
        const auto tenths = [](int v) { return std::to_string(v / 10) + "." + std::to_string(v % 10); };
        const int logic = std::max(0, state.frame_ms_tenths - state.render_ms_tenths - state.audio_ms_tenths);
        const auto line = "Juego " + tenths(logic) + "  Video " + tenths(state.render_ms_tenths) + "  Audio " +
                          tenths(state.audio_ms_tenths) + "  Pant " + tenths(state.present_ms_tenths) + " ms";
        c.text_centered({0, 180, bottom_width, 14}, line, color::dim);
    }

    if (!options_open_) {
        draw_button(c, talk_button, "Hablar", "L");
        draw_button(c, map_button, "Mapa", "X");
    }
    draw_button(c, options_button, options_open_ ? "Volver" : "Opciones", nullptr);
}

void BottomUi::draw_party(Canvas &c, const UiState &state) const {
    const bool any = std::any_of(state.party.begin(), state.party.end(),
                                 [](const eb::PartyMemberStatus &m) { return m.character != 0; });
    if (!any) {
        c.text_centered({0, 90, bottom_width, 14}, "El estado del grupo aparecerá", color::dim);
        c.text_centered({0, 106, bottom_width, 14}, "al empezar a jugar.", color::dim);
        return;
    }
    for (unsigned slot = 0; slot < state.party.size(); ++slot) {
        const auto &m = state.party[slot];
        const Rect card{8 + int(slot % 2) * 156, 30 + int(slot / 2) * 74, 148, 70};
        if (m.character == 0) {
            c.frame(card, color::card_border);
            continue;
        }
        const bool down = m.hp == 0 && m.max_hp != 0;
        c.fill(card, down ? color::knocked_out : color::card);
        c.frame(card, color::card_border);
        c.text(card.x + 6, card.y + 5, character_name(m.character), color::text);
        char level[12];
        std::snprintf(level, sizeof level, "Nv %u", unsigned(m.level));
        c.text(card.x + card.w - 6 - Canvas::text_width(level), card.y + 5, level, color::dim);

        char numbers[16];
        const bool low = m.max_hp && m.hp * 4 <= m.max_hp;
        c.text(card.x + 6, card.y + 21, "HP", color::dim);
        draw_bar(c, card.x + 24, card.y + 23, card.w - 30, m.hp, m.max_hp, low ? color::hp_low : color::hp);
        std::snprintf(numbers, sizeof numbers, "%u/%u", unsigned(m.hp), unsigned(m.max_hp));
        c.text(card.x + card.w - 6 - Canvas::text_width(numbers), card.y + 32, numbers, color::text);

        c.text(card.x + 6, card.y + 45, "PP", color::dim);
        draw_bar(c, card.x + 24, card.y + 47, card.w - 30, m.pp, m.max_pp, color::pp);
        std::snprintf(numbers, sizeof numbers, "%u/%u", unsigned(m.pp), unsigned(m.max_pp));
        c.text(card.x + card.w - 6 - Canvas::text_width(numbers), card.y + 56, numbers, color::text);
    }
}

void BottomUi::draw_profile(Canvas &c, const UiState &state) const {
    // Order and names follow eb::frame_profile::Phase.
    static constexpr const char *names[] = {"Espera vblank", "Pantallas", "Bucle", "Ganchos", "Nativo",
                                            "Ejecución", "Planificador", "Video", "Audio"};
    char header[64];
    std::snprintf(header, sizeof header, "%dk instr/frame  %d ns/instr", state.kinstr_per_frame,
                  state.ns_per_instr);
    c.text(8, 30, header, color::accent);
    for (unsigned i = 0; i < state.phase_percent.size(); ++i) {
        const int y = 46 + int(i) * 14;
        c.text(8, y, names[i], color::dim);
        c.fill({100, y + 2, 120, 8}, color::bar_empty);
        c.fill({100, y + 2, state.phase_percent[i] * 120 / 100, 8}, i >= 3 ? color::hp : color::pp);
        char value[24];
        std::snprintf(value, sizeof value, "%2u%% %3d.%d", unsigned(state.phase_percent[i]),
                      state.phase_ms_tenths[i] / 10, state.phase_ms_tenths[i] % 10);
        c.text(bottom_width - 8 - Canvas::text_width(value), y, value, color::text);
    }
}

void BottomUi::draw_options(Canvas &c, const UiState &state) const {
    const std::string skip = std::to_string(state.frame_skip);
    const struct {
        const char *label;
        std::string value;
        bool active;
    } rows[] = {{"Pantalla panorámica (400 px)", state.widescreen ? "Sí" : "No", state.widescreen},
                {"Mostrar rendimiento",
                 !state.show_performance ? "No" : state.profile_detail ? "Det" : "Sí", state.show_performance},
                {"Modo original (rápido, al reiniciar)", state.original_timing ? "Sí" : "No",
                 state.original_timing},
                {"Saltar frames de video", skip, state.frame_skip > 0},
                {"Salir (guarda la partida)", "", false}};
    for (unsigned i = 0; i < option_rows.size(); ++i) {
        const auto r = option_rows[i];
        c.fill(r, color::card);
        c.frame(r, color::card_border);
        c.text(r.x + 10, r.y + 10, rows[i].label, color::text);
        if (!rows[i].value.empty()) {
            const Rect pill{r.x + r.w - 46, r.y + 6, 38, 18};
            c.fill(pill, rows[i].active ? color::accent : color::button);
            c.text_centered(pill, rows[i].value, rows[i].active ? color::background : color::text);
        }
    }
}

} // namespace pd3ds
