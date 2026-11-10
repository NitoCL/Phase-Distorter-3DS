#include "audio_3ds.hpp"

#include <algorithm>
#include <cstring>

namespace pd3ds {
namespace {
constexpr int channel = 0;
bool busy(const ndspWaveBuf &buffer) {
    return buffer.status == NDSP_WBUF_QUEUED || buffer.status == NDSP_WBUF_PLAYING;
}
} // namespace

Audio3ds::Audio3ds() {
    if (R_FAILED(ndspInit())) return; // Usually a missing dspfirm.cdc.
    const std::size_t bytes = buffer_count * frames_per_buffer * 2 * sizeof(std::int16_t);
    memory_ = static_cast<std::int16_t *>(linearAlloc(bytes));
    if (!memory_) {
        ndspExit();
        return;
    }
    std::memset(memory_, 0, bytes);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(channel);
    ndspChnSetInterp(channel, NDSP_INTERP_LINEAR);
    ndspChnSetRate(channel, float(sample_rate));
    ndspChnSetFormat(channel, NDSP_FORMAT_STEREO_PCM16);
    float mix[12]{};
    mix[0] = mix[1] = 1.0f; // Front left / front right.
    ndspChnSetMix(channel, mix);
    for (std::size_t i = 0; i < buffer_count; ++i) {
        buffers_[i] = {};
        buffers_[i].data_vaddr = memory_ + i * frames_per_buffer * 2;
    }
    available_ = true;
}

Audio3ds::~Audio3ds() {
    if (!available_) return;
    ndspChnWaveBufClear(channel);
    ndspExit();
    linearFree(memory_);
}

void Audio3ds::pause(bool paused) {
    if (available_) ndspChnSetPaused(channel, paused);
}

void Audio3ds::queue(std::span<const std::int16_t> stereo) {
    if (!available_) return;
    const std::size_t frames = stereo.size() / 2;
    std::size_t offset = 0;
    while (offset < frames) {
        if (fill_frames_ == 0 && busy(buffers_[fill_index_])) {
            // The DSP still owns this slot: the core is producing faster than
            // playback. Drop rather than block the game loop.
            dropped_ += frames - offset;
            return;
        }
        const std::size_t count = std::min(frames_per_buffer - fill_frames_, frames - offset);
        std::memcpy(memory_ + (fill_index_ * frames_per_buffer + fill_frames_) * 2, stereo.data() + offset * 2,
                    count * 2 * sizeof(std::int16_t));
        fill_frames_ += count;
        offset += count;
        if (fill_frames_ == frames_per_buffer) submit_pending();
    }
}

void Audio3ds::submit_pending() {
    const auto queued = std::count_if(buffers_.begin(), buffers_.end(), busy);
    auto &buffer = buffers_[fill_index_];
    if (std::size_t(queued) >= max_queued) {
        dropped_ += fill_frames_;
    } else {
        buffer.nsamples = std::uint32_t(fill_frames_);
        DSP_FlushDataCache(buffer.data_vaddr, fill_frames_ * 2 * sizeof(std::int16_t));
        ndspChnWaveBufAdd(channel, &buffer);
    }
    fill_frames_ = 0;
    fill_index_ = (fill_index_ + 1) % buffer_count;
}

} // namespace pd3ds
