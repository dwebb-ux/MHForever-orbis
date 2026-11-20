#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace mhf::runtime {

enum class AudioFormat : std::uint8_t {
    PcmI16Stereo,
};

struct AudioConfig {
    std::uint32_t sample_rate = 48000;
    std::uint16_t channels = 2;
};

class IAudioSystem {
public:
    virtual ~IAudioSystem() = default;

    virtual bool Initialize(const AudioConfig &config) = 0;
    virtual void Shutdown() noexcept = 0;
    virtual void Queue(std::span<const std::int16_t> interleaved_samples) = 0;
    virtual std::size_t QueuedFrames() const noexcept = 0;
};

} // namespace mhf::runtime
