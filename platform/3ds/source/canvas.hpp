#pragma once
// Software drawing into a 3DS LCD framebuffer. Platform-independent so the
// bottom-screen UI can be rendered and checked on a desktop host.
//
// 3DS framebuffers are stored rotated: the panel is 240 pixels tall and the
// memory is column-major, bottom row first. Pixel (x, y) of a W x 240 screen
// lives at ((x * 240) + (239 - y)) * 3, with bytes in B, G, R order (BGR8).
#include "font_6x11.hpp"
#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>

namespace pd3ds {

constexpr int screen_height = 240;
constexpr int top_width = 400;
constexpr int bottom_width = 320;

// 0xRRGGBB. Matches the low 24 bits of the core's 0xAARRGGBB pixels.
using Color = std::uint32_t;

struct Rect {
    int x{}, y{}, w{}, h{};
    constexpr bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

class Canvas {
  public:
    Canvas(std::uint8_t *framebuffer, int width) : fb_(framebuffer), width_(width) {}
    int width() const { return width_; }

    void pixel(int x, int y, Color c) {
        if (x < 0 || y < 0 || x >= width_ || y >= screen_height) return;
        auto *p = fb_ + (x * screen_height + (screen_height - 1 - y)) * 3;
        p[0] = std::uint8_t(c);
        p[1] = std::uint8_t(c >> 8);
        p[2] = std::uint8_t(c >> 16);
    }

    void fill(Rect r, Color c) {
        const int x0 = std::max(r.x, 0), x1 = std::min(r.x + r.w, width_);
        const int y0 = std::max(r.y, 0), y1 = std::min(r.y + r.h, screen_height);
        if (x0 >= x1 || y0 >= y1) return;
        const std::uint8_t b = std::uint8_t(c), g = std::uint8_t(c >> 8), rr = std::uint8_t(c >> 16);
        for (int x = x0; x < x1; ++x) {
            // Rows y1-1 .. y0 are contiguous in memory for a fixed column.
            auto *p = fb_ + (x * screen_height + (screen_height - y1)) * 3;
            for (int n = y1 - y0; n > 0; --n, p += 3) {
                p[0] = b;
                p[1] = g;
                p[2] = rr;
            }
        }
    }

    void clear(Color c) { fill({0, 0, width_, screen_height}, c); }

    void frame(Rect r, Color c, int thickness = 1) {
        fill({r.x, r.y, r.w, thickness}, c);
        fill({r.x, r.y + r.h - thickness, r.w, thickness}, c);
        fill({r.x, r.y, thickness, r.h}, c);
        fill({r.x + r.w - thickness, r.y, thickness, r.h}, c);
    }

    // UTF-8 text limited to Latin-1, enough for Spanish. Returns advance width.
    int text(int x, int y, std::string_view utf8, Color c, int scale = 1) {
        int cursor = x;
        for_each_codepoint(utf8, [&](std::uint32_t cp) {
            const auto &glyph = font::glyphs[font::glyph_index(cp)];
            for (int row = 0; row < font::height; ++row)
                for (int col = 0; col < font::width; ++col)
                    if (glyph[row] & (1u << (font::width - 1 - col)))
                        fill({cursor + col * scale, y + row * scale, scale, scale}, c);
            cursor += font::width * scale;
        });
        return cursor - x;
    }

    static int text_width(std::string_view utf8, int scale = 1) {
        int n = 0;
        for_each_codepoint(utf8, [&](std::uint32_t) { ++n; });
        return n * font::width * scale;
    }

    void text_centered(Rect r, std::string_view utf8, Color c, int scale = 1) {
        text(r.x + (r.w - text_width(utf8, scale)) / 2, r.y + (r.h - font::height * scale) / 2, utf8, c, scale);
    }

    // Centered blit of a row-major 0xAARRGGBB image (the core's format).
    // Letterbox areas are left untouched; callers clear them on mode changes.
    void blit_centered(std::span<const std::uint32_t> pixels, int src_width, int src_height) {
        const int w = std::min(src_width, width_), h = std::min(src_height, screen_height);
        const int src_x0 = (src_width - w) / 2, src_y0 = (src_height - h) / 2;
        const int dst_x0 = (width_ - w) / 2, dst_y0 = (screen_height - h) / 2;
        for (int x = 0; x < w; ++x) {
            const std::uint32_t *src = pixels.data() + src_y0 * src_width + src_x0 + x;
            auto *dst = fb_ + ((dst_x0 + x) * screen_height + (screen_height - 1 - dst_y0)) * 3;
            for (int y = 0; y < h; ++y, src += src_width, dst -= 3) {
                const std::uint32_t p = *src;
                dst[0] = std::uint8_t(p);
                dst[1] = std::uint8_t(p >> 8);
                dst[2] = std::uint8_t(p >> 16);
            }
        }
    }

  private:
    template <class F> static void for_each_codepoint(std::string_view s, F &&emit) {
        for (std::size_t i = 0; i < s.size();) {
            const auto b0 = std::uint8_t(s[i]);
            if (b0 < 0x80) {
                emit(b0);
                i += 1;
            } else if ((b0 & 0xE0) == 0xC0 && i + 1 < s.size()) {
                emit(((b0 & 0x1Fu) << 6) | (std::uint8_t(s[i + 1]) & 0x3Fu));
                i += 2;
            } else {
                emit('?');
                i += 1;
                while (i < s.size() && (std::uint8_t(s[i]) & 0xC0) == 0x80) ++i;
            }
        }
    }

    std::uint8_t *fb_;
    int width_;
};

} // namespace pd3ds
