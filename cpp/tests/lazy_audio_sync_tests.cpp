// Lazy APU synchronization must be observably identical to forwarding every
// elapsed clock immediately. Two machines run the same randomized CPU-side
// schedule against the SPC700 boot ROM: DSP setup for an audible noise voice,
// then random transfer blocks interleaved with random clock advances that
// cross scanlines and frames. Every port value the CPU reads, the APU's
// instruction/cycle counts and every generated stereo sample must match.
#include "eb/snes_audio_dsp.hpp"
#include "eb/snes_bus.hpp"
#include "eb/spc700_audio_cpu.hpp"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace {
struct Result {
    std::vector<std::uint8_t> reads;
    std::vector<std::int16_t> samples;
    std::uint64_t instructions{}, cycles{}, frames{};
};

Result run(bool lazy, std::uint32_t seed) {
    std::vector<std::uint8_t> rom(0x300000);
    eb::SnesBus bus(rom);
    eb::Spc700AudioCpu apu(bus);
    eb::SnesAudioDsp dsp(apu);
    bus.set_lazy_audio_sync(lazy);
    std::mt19937 random(seed);
    Result result;
    std::vector<std::int16_t> samples;

    const auto tick = [&] { bus.advance_master_clocks_with_refresh(6 * (1 + random() % 40)); };
    const auto read = [&](unsigned port) {
        const auto value = bus.read_byte(0x2140 + port);
        result.reads.push_back(value);
        return value;
    };
    const auto write = [&](unsigned port, std::uint8_t value) { bus.write_byte(0x2140 + port, value); };
    const auto wait_for = [&](unsigned port, std::uint8_t value) {
        for (int guard = 0; read(port) != value; ++guard) {
            if (guard > 2'000'000) {
                std::fprintf(stderr, "timeout waiting for port %u = %02x\n", port, value);
                std::exit(1);
            }
            tick();
        }
    };

    // Drive the SPC700 boot ROM (IPL) transfer protocol. Two-byte blocks to
    // $00F2 write DSP registers ($F2 = index, $F3 = value), configuring an
    // audible noise voice; then many random data blocks go to RAM, with random
    // clock advances and extra port reads interleaved throughout.
    std::uint8_t counter = 0;
    bool first_block = true;
    const auto begin_block = [&](std::uint16_t address) {
        write(2, std::uint8_t(address));
        write(3, std::uint8_t(address >> 8));
        write(1, 1); // Nonzero: another transfer block follows.
        std::uint8_t kick = first_block ? 0xCC : std::uint8_t(counter + 2);
        if (!first_block && kick == 0) kick = 1;
        first_block = false;
        write(0, kick);
        wait_for(0, kick);
        counter = 0xFF; // Data indices restart at 0.
    };
    const auto send = [&](std::uint8_t value) {
        counter = std::uint8_t(counter + 1);
        write(1, value);
        write(0, counter);
        wait_for(0, counter);
        if (random() % 3 == 0) tick();
        if (random() % 5 == 0) read(random() % 4);
    };
    const auto dsp_write = [&](std::uint8_t reg, std::uint8_t value) {
        begin_block(0x00F2);
        send(reg);
        send(value);
    };

    wait_for(0, 0xAA);
    wait_for(1, 0xBB);
    // Sample directory at $0200 -> one looping silent BRR block at $0300, so
    // the voice never reads the IPL's zero-page variables as sample data.
    begin_block(0x0200);
    for (std::uint8_t byte : {0x00, 0x03, 0x00, 0x03}) send(byte);
    begin_block(0x0300);
    send(0x03); // BRR header: end + loop.
    for (int i = 0; i < 8; ++i) send(0x00);
    dsp_write(0x6C, 0x20 | 0x1C); // FLG: unmute, no echo writes, fast noise clock.
    dsp_write(0x0C, 0x7F);        // MVOL L
    dsp_write(0x1C, 0x7F);        // MVOL R
    dsp_write(0x00, 0x7F);        // V0 VOL L
    dsp_write(0x01, 0x60);        // V0 VOL R
    dsp_write(0x5D, 0x02);        // DIR = $0200
    dsp_write(0x04, 0x00);        // V0 SRCN 0 (power-on registers hold garbage)
    dsp_write(0x05, 0x00);        // ADSR1 off: use GAIN
    dsp_write(0x07, 0x7F);        // GAIN direct
    dsp_write(0x3D, 0x01);        // NON voice 0
    dsp_write(0x5C, 0x00);        // KOFF: power-on state may hold voices off
    dsp_write(0x4C, 0x01);        // KON voice 0

    for (int block = 0; block < 60; ++block) {
        begin_block(std::uint16_t(0x0400 + (random() % 0x4000)));
        const int length = 1 + int(random() % 40);
        for (int i = 0; i < length; ++i) send(std::uint8_t(random()));
        for (int idle = int(random() % 400); idle > 0; --idle) {
            tick();
            if (random() % 7 == 0) read(random() % 4);
        }
        if (block % 10 == 0) { // Hosts drain audio at frame ends.
            bus.sync_audio();
            auto chunk = dsp.take_stereo_samples();
            samples.insert(samples.end(), chunk.begin(), chunk.end());
        }
    }
    bus.sync_audio();
    auto chunk = dsp.take_stereo_samples();
    samples.insert(samples.end(), chunk.begin(), chunk.end());
    result.samples = std::move(samples);
    result.instructions = apu.instruction_count;
    result.cycles = apu.cycle_count;
    result.frames = bus.completed_frames;
    return result;
}
} // namespace

int main() {
    for (std::uint32_t seed : {1u, 2u, 3u}) {
        const auto eager = run(false, seed), lazy = run(true, seed);
        unsigned nonzero = 0;
        for (auto s : eager.samples) nonzero += s != 0;
        std::printf("seed %u: frames=%llu reads=%zu apu_instr=%llu samples=%zu nonzero=%u\n", seed,
                    (unsigned long long)eager.frames, eager.reads.size(), (unsigned long long)eager.instructions,
                    eager.samples.size() / 2, nonzero);
        if (eager.reads != lazy.reads || eager.samples != lazy.samples || eager.instructions != lazy.instructions ||
            eager.cycles != lazy.cycles || eager.frames != lazy.frames) {
            std::fprintf(stderr, "MISMATCH (reads %d, samples %d, instr %d, cycles %d)\n", eager.reads != lazy.reads,
                         eager.samples != lazy.samples, eager.instructions != lazy.instructions,
                         eager.cycles != lazy.cycles);
            return 1;
        }
        if (eager.samples.empty() || nonzero == 0) {
            std::fprintf(stderr, "test did not exercise audio output\n");
            return 1;
        }
    }
    std::puts("lazy audio sync: identical to eager forwarding");
    return 0;
}
