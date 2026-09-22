#include "platform/sdl/audio_output.hpp"

#include <climits>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace ghostbusters::platform::sdl {
namespace {

std::runtime_error sdl_error(const char* operation)
{
    return std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

} // namespace

AudioOutput::AudioOutput(SDL_AudioDeviceID device)
{
    const SDL_AudioSpec spec{
        .format = SDL_AUDIO_F32,
        .channels = 1,
        .freq = 48000,
    };
    stream_ = SDL_OpenAudioDeviceStream(device, &spec, nullptr, nullptr);
    if (stream_ == nullptr) {
        throw sdl_error("SDL_OpenAudioDeviceStream");
    }
    if (!SDL_ResumeAudioStreamDevice(stream_)) {
        const auto error = sdl_error("SDL_ResumeAudioStreamDevice");
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        throw error;
    }
}

AudioOutput::~AudioOutput() noexcept
{
    if (stream_ != nullptr) SDL_DestroyAudioStream(stream_);
}

AudioOutput::AudioOutput(AudioOutput&& other) noexcept
    : stream_(other.stream_)
{
    other.stream_ = nullptr;
}

AudioOutput& AudioOutput::operator=(AudioOutput&& other) noexcept
{
    if (this == &other) return *this;
    if (stream_ != nullptr) SDL_DestroyAudioStream(stream_);
    stream_ = other.stream_;
    other.stream_ = nullptr;
    return *this;
}

void AudioOutput::push(std::span<const float> samples)
{
    if (samples.empty()) return;
    constexpr auto max_samples = static_cast<std::size_t>(INT_MAX) / sizeof(float);
    if (samples.size() > max_samples) {
        throw std::length_error("Audio sample span is too large for SDL");
    }
    const auto bytes = static_cast<int>(samples.size() * sizeof(float));
    if (!SDL_PutAudioStreamData(stream_, samples.data(), bytes)) {
        throw sdl_error("SDL_PutAudioStreamData");
    }
}

std::size_t AudioOutput::queued_frames() const
{
    const auto bytes = SDL_GetAudioStreamQueued(stream_);
    if (bytes < 0) throw sdl_error("SDL_GetAudioStreamQueued");
    return static_cast<std::size_t>(bytes) / sizeof(float);
}

void AudioOutput::clear()
{
    if (!SDL_ClearAudioStream(stream_)) throw sdl_error("SDL_ClearAudioStream");
}

} // namespace ghostbusters::platform::sdl
