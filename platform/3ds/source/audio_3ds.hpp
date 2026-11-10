#pragma once
// Streams the core's 32 kHz interleaved stereo PCM16 through the 3DS DSP.
// Requires sdmc:/3ds/dspfirm.cdc (dumped with DSP1); without it the game
// still runs, muted.
#include <3ds.h>
#include <array>
#include <cstdint>
#include <span>

namespace pd3ds {

class Audio3ds {
  public:
    static constexpr int sample_rate = 32000;
    // ~32 ms per buffer; at most `max_queued` buffers bound the latency.
    static constexpr std::size_t frames_per_buffer = 1024;
    static constexpr std::size_t buffer_count = 8;
    static constexpr std::size_t max_queued = 4;

    Audio3ds();
    ~Audio3ds();
    Audio3ds(const Audio3ds &) = delete;
    Audio3ds &operator=(const Audio3ds &) = delete;

    bool available() const { return available_; }
    // Interleaved L,R samples. Excess is dropped instead of growing latency.
    void queue(std::span<const std::int16_t> stereo);
    void pause(bool paused);
    std::uint64_t dropped_frames() const { return dropped_; }

  private:
    void submit_pending();

    bool available_ = false;
    std::int16_t *memory_ = nullptr; // linearAlloc'd, DSP-visible.
    std::array<ndspWaveBuf, buffer_count> buffers_{};
    std::size_t fill_index_ = 0;   // Buffer currently being filled.
    std::size_t fill_frames_ = 0;  // Frames already in that buffer.
    std::uint64_t dropped_ = 0;
};

} // namespace pd3ds
