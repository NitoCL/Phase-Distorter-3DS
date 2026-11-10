// The whole-scanline native compositor must produce exactly the picture of
// the per-pixel compositor. Random PPU states (all BG modes, tile sizes,
// mosaic, scroll, windows and their logic, color math, fixed color,
// brightness, OBJ and mode 7) are rendered for a full frame by two buses that
// differ only in set_fast_native_compose; every framebuffer pixel must match.
//
//   ppu_compose_fast_tests            differential check
//   ppu_compose_fast_tests bench N    time N frames with each compositor
#include "eb/snes_bus.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

namespace {
constexpr unsigned frame_clocks = 262 * 1364;

void randomize(eb::SnesBus &bus, std::uint32_t seed) {
    std::mt19937 random(seed);
    const auto byte = [&] { return std::uint8_t(random()); };
    const auto chance = [&](unsigned percent) { return random() % 100 < percent; };
    for (auto &b : bus.video_ram) b = byte();
    for (auto &b : bus.palette_ram) b = byte();
    for (auto &b : bus.object_attributes) b = byte();
    const auto write = [&](unsigned address, std::uint8_t value) { bus.write_byte(address, value); };

    static constexpr unsigned modes[] = {1, 1, 1, 1, 0, 2, 3, 4, 5, 6, 7};
    const unsigned mode = modes[random() % std::size(modes)];
    write(0x2101, byte());
    write(0x2105, std::uint8_t(mode | (chance(30) ? 8 : 0) | (chance(30) ? (byte() & 0xf0) : 0)));
    write(0x2106, chance(20) ? byte() : 0);
    for (unsigned r = 0x2107; r <= 0x210c; ++r) write(r, byte());
    for (unsigned r = 0x210d; r <= 0x2114; ++r) {
        write(r, byte());
        write(r, byte() & 3);
    }
    write(0x211a, byte());
    for (unsigned r = 0x211b; r <= 0x2120; ++r) {
        write(r, byte());
        write(r, byte());
    }
    for (unsigned r = 0x2123; r <= 0x212b; ++r) write(r, chance(60) ? byte() : 0);
    write(0x212c, byte() & 0x1f);
    write(0x212d, byte() & 0x1f);
    write(0x212e, chance(50) ? byte() & 0x1f : 0);
    write(0x212f, chance(50) ? byte() & 0x1f : 0);
    write(0x2130, chance(70) ? std::uint8_t(byte() & (chance(70) ? 0x3f : 0xff)) : 0);
    write(0x2131, chance(70) ? byte() : 0);
    for (int i = 0; i < 3; ++i) write(0x2132, byte());
    write(0x2133, byte() & 0x4c);
    // Mostly visible pictures; dim, black and forced-blank screens still occur.
    write(0x2100, std::uint8_t(chance(70) ? 15 : chance(85) ? (random() % 16) : 0x80 | (random() % 16)));
}

std::vector<std::uint32_t> render(std::uint32_t seed, bool fast) {
    std::vector<std::uint8_t> rom(0x300000);
    eb::SnesBus bus(rom);
    bus.set_fast_native_compose(fast);
    randomize(bus, seed);
    for (unsigned clocks = 0; clocks < frame_clocks + 2000; clocks += 1360)
        bus.advance_master_clocks_with_refresh(1360);
    return {bus.native_framebuffer.begin(), bus.native_framebuffer.end()};
}

int bench(int frames) {
    const bool only_fast = std::getenv("PPU_BENCH_FAST_ONLY") != nullptr;
    for (bool fast : {false, true}) {
        if (only_fast && !fast) continue;
        std::vector<std::uint8_t> rom(0x300000);
        eb::SnesBus bus(rom);
        bus.set_fast_native_compose(fast);
        randomize(bus, 1234);
        bus.write_byte(0x2105, 0x09); // Typical overworld: mode 1, BG3 priority.
        const auto start = std::chrono::steady_clock::now();
        for (unsigned clocks = 0; clocks < unsigned(frames) * frame_clocks; clocks += 1360)
            bus.advance_master_clocks_with_refresh(1360);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("%s: %.3f ms/frame\n", fast ? "fast   " : "original", ms / frames);
    }
    return 0;
}
} // namespace

int main(int argc, char **argv) {
    if (argc > 2 && !std::strcmp(argv[1], "bench")) return bench(std::atoi(argv[2]));
    const unsigned states = argc > 1 ? unsigned(std::atoi(argv[1])) : 400;
    unsigned differing = 0, varied = 0;
    for (std::uint32_t seed = 1; seed <= states; ++seed) {
        const auto reference = render(seed, false), fast = render(seed, true);
        std::vector<std::uint32_t> colors(reference);
        std::sort(colors.begin(), colors.end());
        varied += std::unique(colors.begin(), colors.end()) - colors.begin() > 64;
        if (reference != fast) {
            unsigned first = 0;
            while (reference[first] == fast[first]) ++first;
            std::fprintf(stderr, "seed %u: pixel (%u,%u) %08x != %08x\n", seed, first % 256, first / 256,
                         reference[first], fast[first]);
            if (++differing > 5) break;
        }
    }
    if (differing) return 1;
    // Guard against a vacuous pass: most random states must draw real pictures.
    if (varied * 2 < states) {
        std::fprintf(stderr, "only %u of %u states produced varied pictures\n", varied, states);
        return 1;
    }
    std::printf("%u of %u states drew more than 64 distinct colors\n", varied, states);
    std::printf("fast native compositor: %u random PPU states, every pixel identical\n", states);
    return 0;
}
