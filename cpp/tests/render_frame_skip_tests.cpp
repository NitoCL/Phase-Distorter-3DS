// Video frame skipping must only withhold pictures: hardware timing, interrupt
// state, HDMA-driven registers and frame counts stay identical, and exactly one
// frame in every (skip + 1) reports a rendered picture.
#include "eb/snes_bus.hpp"

#include <cstdio>
#include <vector>

namespace {
struct Trace {
    std::vector<std::uint64_t> master_clocks;
    std::vector<unsigned> scanlines;
    std::vector<bool> rendered;
    std::uint64_t frames{};
};

Trace run(unsigned skip) {
    std::vector<std::uint8_t> rom(0x300000);
    eb::SnesBus bus(rom);
    bus.set_render_frame_skip(skip);
    bus.write_byte(0x2100, 0x0F);  // Display on, full brightness: renders real lines.
    bus.write_byte(0x4200, 0x81);  // NMI + auto-joypad enabled.
    Trace trace;
    auto frame = bus.completed_frames;
    for (int step = 0; step < 400000; ++step) {
        bus.advance_master_clocks_with_refresh(6 * (1 + step % 7));
        if (step % 97 == 0) {
            trace.master_clocks.push_back(bus.master_clocks());
            trace.scanlines.push_back(bus.scanline_index());
        }
        if (bus.completed_frames != frame) {
            frame = bus.completed_frames;
            trace.rendered.push_back(bus.last_frame_rendered());
        }
    }
    trace.frames = bus.completed_frames;
    return trace;
}
} // namespace

int main() {
    const auto reference = run(0);
    for (unsigned skip : {0u, 1u, 2u}) {
        const auto trace = run(skip);
        if (trace.master_clocks != reference.master_clocks || trace.scanlines != reference.scanlines ||
            trace.frames != reference.frames) {
            std::fprintf(stderr, "skip %u changed hardware timing\n", skip);
            return 1;
        }
        unsigned rendered = 0;
        for (std::size_t i = 0; i < trace.rendered.size(); ++i) {
            // Entry i is frame n = i + 1; its picture was decided when frame
            // n - 1 completed, so frames 1, 1 + (skip + 1), ... are drawn.
            const bool expected = i % (skip + 1) == 0;
            if (trace.rendered[i] != expected) {
                std::fprintf(stderr, "skip %u: frame %zu rendered=%d\n", skip, i + 1, int(trace.rendered[i]));
                return 1;
            }
            rendered += trace.rendered[i];
        }
        std::printf("skip %u: %llu frames, %u rendered\n", skip, (unsigned long long)trace.frames, rendered);
    }
    std::puts("render frame skip: timing identical, pictures withheld as configured");
    return 0;
}
