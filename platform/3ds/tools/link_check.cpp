// Links the exact core API the 3DS frontend uses into one static ARMv6K
// binary, proving no desktop/SDL/OpenGL symbol is required and measuring
// code size. With an asset pack argument it also runs N headless frames:
//   link_check earthbound.ebpak 600
#include "../source/bottom_ui.hpp"
#include "eb/asset_store.hpp"
#include "eb/game_session.hpp"
#include "eb/logical_clock_policy.hpp"
#include "generated_assets.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char **argv) {
    if (argc < 2) {
        std::printf("asset profiles linked: %zu\n", eb::asset_profiles().size());
        return 0;
    }
    const auto profiles = eb::asset_profiles();
    const auto program = eb::load_game_assets(argv[1], profiles);
    eb::GameSession session(program.image, program.version, true);
    session.set_logical_clock_policy(eb::LogicalClockPolicy::ActorFrames);
    session.enable_native_sprite_runtime();
    session.configure_presentation(256, false, false);
    const int frames = argc > 2 ? std::atoi(argv[2]) : 600;
    std::vector<std::uint8_t> fb(pd3ds::top_width * pd3ds::screen_height * 3);
    pd3ds::Canvas top(fb.data(), pd3ds::top_width);
    const auto start = std::chrono::steady_clock::now();
    std::size_t samples = 0;
    for (int i = 0; i < frames; ++i) {
        // Tap Start periodically to move past the title screens.
        session.advance_frame((i % 120) < 4 ? pd3ds::snes::Start : 0);
        samples += session.take_audio_samples().size() / 2;
        const auto frame = session.presentation_frame();
        top.blit_centered(frame.pixels, int(frame.width), int(frame.pixels.size() / frame.width));
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const auto party = session.party_status();
    std::printf("%s: %d frames, %.2f ms/frame, %zu stereo samples, party[0]=%u\n", program.title.c_str(), frames,
                ms / frames, samples, unsigned(party[0].character));
    return 0;
}
