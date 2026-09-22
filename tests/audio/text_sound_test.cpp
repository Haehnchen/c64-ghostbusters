#include "audio/text_sound.hpp"
#include "audio/scene_audio.hpp"
#include "assets/payload.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using ghostbusters::audio::SidWrite;
using ghostbusters::audio::TextSound;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void testToneLoadsVolume()
{
    TextSound sound;
    const std::vector<SidWrite> expected{
        {0x12, 0x08}, {0x18, 0x0F}, {0x0E, 0x12}, {0x0F, 0x34},
        {0x10, 0x80}, {0x11, 0x0F}, {0x13, 0x03}, {0x14, 0x00},
        {0x12, 0x41},
    };
    check(sound.text_tone() == expected,
          "a cold text tone preserves the exact SceneAudio write order");
    check(sound.volume() == 0x0F,
          "the first text tone updates the volume cache to fifteen");
}

void testToneUsesCachedVolume()
{
    TextSound sound(0x0F);
    const std::vector<SidWrite> expected{
        {0x12, 0x08}, {0x0E, 0x12}, {0x0F, 0x34}, {0x10, 0x80},
        {0x11, 0x0F}, {0x13, 0x03}, {0x14, 0x00}, {0x12, 0x41},
    };
    check(sound.text_tone() == expected,
          "a cached fifteen volume omits the redundant D418 write");
    check(sound.volume() == 0x0F, "cached fifteen remains unchanged");

    TextSound nonzero(7);
    static_cast<void>(nonzero.text_tone());
    check(nonzero.volume() == 0x0F,
          "any non-fifteen initial cache is restored by text_tone");
}

void testKeyClickAndCache()
{
    TextSound sound(3);
    const std::vector<SidWrite> expected{{0x12, 0x00}, {0x12, 0x41}};
    check(sound.key_click() == expected,
          "keyboard click writes only the two voice-3 gate values");
    check(sound.volume() == 3,
          "keyboard click does not change the volume cache");

    check(sound.key_click() == expected,
          "keyboard clicks remain repeatable and deterministic");
}

void testAllInitialCacheValues()
{
    for (unsigned initial = 0; initial <= 0xFF; ++initial) {
        TextSound sound(static_cast<std::uint8_t>(initial));
        const auto writes = sound.text_tone();
        const auto expected_size = initial == 0x0F ? 8U : 9U;
        check(writes.size() == expected_size,
              "only cached volume fifteen omits D418");
        check(sound.volume() == 0x0F,
              "text tone normalizes every other cache value");
    }
}

void testPromptAndTypingPcm()
{
    using ghostbusters::audio::SceneAudio;
    const auto payload = ghostbusters::assets::Payload::embedded();
    SceneAudio audio(payload, SceneAudio::StartPoint::title_music);
    for (unsigned frame = 0; frame < 30; ++frame) (void)audio.frame();
    audio.leave_title();

    float peak = 0;
    std::size_t samples = 0;
    std::size_t rail_samples = 0;
    // Prompt text, rapid typing, slower typing, then envelope decay. Gate
    // transients are intentional; this checks PCM validity, not perceived timbre.
    for (unsigned frame = 0; frame < 240; ++frame) {
        audio.begin_frame();
        if (frame < 40 && frame % 2 == 0) audio.text_tone();
        if ((frame >= 40 && frame < 100 && frame % 2 == 0) ||
            (frame >= 100 && frame < 180 && frame % 10 == 0)) audio.key_click();
        const auto pcm = audio.finish_frame();
        check(!pcm.empty(), "prompt/typing frame produces PCM");
        for (const float value : pcm) {
            check(std::isfinite(value), "prompt/typing PCM is finite");
            peak = std::max(peak, std::abs(value));
            if (value <= -1.0F || value >= 32767.0F / 32768.0F) ++rail_samples;
        }
        samples += pcm.size();
    }
    check(peak > 0, "prompt/typing audio is not silent");
    check(rail_samples == 0, "prompt/typing PCM does not reach the signed-16-bit rails");
    std::cout << "Prompt/typing PCM: " << samples << " samples, peak " << peak
              << ", rail samples " << rail_samples << '\n';
}

} // namespace

int main()
{
    try {
        testToneLoadsVolume();
        testToneUsesCachedVolume();
        testKeyClickAndCache();
        testAllInitialCacheValues();
        testPromptAndTypingPcm();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "text sound tests passed\n";
    return 0;
}
