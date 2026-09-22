#pragma once

#include <SDL3/SDL_audio.h>

#include <cstddef>
#include <span>

namespace ghostbusters::platform::sdl {

// A small queued sink for the native port's audio layer. SDL owns the device
// and performs any device-side conversion; callers provide mono float samples
// at the fixed 48 kHz application rate.
class AudioOutput {
public:
    explicit AudioOutput(
        SDL_AudioDeviceID device = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
    ~AudioOutput() noexcept;

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;
    AudioOutput(AudioOutput&& other) noexcept;
    AudioOutput& operator=(AudioOutput&& other) noexcept;

    // Queue interleaved mono float samples. The input span is copied by SDL;
    // its storage may be reused as soon as this function returns.
    void push(std::span<const float> samples);
    void clear();

    // Number of queued source sample frames. Since the stream input is mono,
    // one float is one frame. SDL reports queued source bytes for this stream.
    [[nodiscard]] std::size_t queued_frames() const;

private:
    SDL_AudioStream* stream_ = nullptr;
};

} // namespace ghostbusters::platform::sdl
