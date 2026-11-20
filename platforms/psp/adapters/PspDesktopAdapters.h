#pragma once

#include "host/sdl_host.hpp"

#include "interfaces/IAudioSystem.h"
#include "interfaces/IInputSystem.h"
#include "interfaces/IRenderer.h"

#include <memory>

namespace mhf::psp::adapters {

class PspRenderer final : public runtime::IRenderer {
public:
    PspRenderer();
    explicit PspRenderer(host::SdlHost *host);
    ~PspRenderer() override;

    bool Initialize(const runtime::RenderConfig &config) override;
    void Shutdown() noexcept override;
    void BeginFrame() override;
    void Submit(std::span<const std::byte> commands) override;
    void EndFrame() override;

    [[nodiscard]] host::SdlHost *Host() noexcept { return host_; }

private:
    std::unique_ptr<host::SdlHost> owned_host_;
    host::SdlHost *host_;
    runtime::RenderConfig config_;
    bool initialized_ = false;
};

class PspInputSystem final : public runtime::IInputSystem {
public:
    explicit PspInputSystem(host::SdlHost &host);

    bool Poll() override;
    runtime::InputState State() const noexcept override;

private:
    host::SdlHost &host_;
    runtime::InputState state_;
};

class PspAudioSystem final : public runtime::IAudioSystem {
public:
    explicit PspAudioSystem(host::SdlHost &host);

    bool Initialize(const runtime::AudioConfig &config) override;
    void Shutdown() noexcept override;
    void Queue(std::span<const std::int16_t> interleaved_samples) override;
    std::size_t QueuedFrames() const noexcept override;

private:
    host::SdlHost &host_;
    bool initialized_ = false;
};

} // namespace mhf::psp::adapters
