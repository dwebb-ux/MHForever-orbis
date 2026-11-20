#include "PspDesktopAdapters.h"

#include <cstring>

namespace mhf::psp::adapters {

PspRenderer::PspRenderer()
    : owned_host_(std::make_unique<host::SdlHost>()), host_(owned_host_.get()) {}

PspRenderer::PspRenderer(host::SdlHost *host)
    : owned_host_(nullptr), host_(host) {}

PspRenderer::~PspRenderer() = default;

bool PspRenderer::Initialize(const runtime::RenderConfig &config) {
    if (host_ == nullptr) return false;
    config_ = config;
    host::HostOptions options;
    options.title = "MH-Forever";
    options.width = static_cast<int>(config.width);
    options.height = static_cast<int>(config.height);
    options.scale = 2;
    options.audio_rate = 48000;
    if (!host_->open(options)) return false;
    initialized_ = true;
    return true;
}

void PspRenderer::Shutdown() noexcept {
    initialized_ = false;
}

void PspRenderer::BeginFrame() {}

void PspRenderer::Submit(std::span<const std::byte> commands) {
    if (!initialized_ || host_ == nullptr) return;
    if (commands.size_bytes() < config_.width * sizeof(std::uint32_t) * config_.height) return;
    host_->present(
        reinterpret_cast<const std::uint32_t *>(commands.data()),
        static_cast<int>(config_.width),
        static_cast<int>(config_.height),
        static_cast<int>(config_.width));
}

void PspRenderer::EndFrame() {}

PspInputSystem::PspInputSystem(host::SdlHost &host)
    : host_(host) {}

bool PspInputSystem::Poll() {
    if (!host_.poll()) return false;
    const host::GamepadState input = host_.input();
    state_.actions[static_cast<std::size_t>(runtime::InputAction::South)] = input.south;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::East)] = input.east;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::West)] = input.west;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::North)] = input.north;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::LeftShoulder)] = input.left_shoulder;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::RightShoulder)] = input.right_shoulder;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::Start)] = input.start;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::Select)] = input.select;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::DPadUp)] = input.up;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::DPadDown)] = input.down;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::DPadLeft)] = input.left;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::DPadRight)] = input.right;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::LeftTrigger)] = false;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::RightTrigger)] = false;
    state_.left_x = input.left_x;
    state_.left_y = input.left_y;
    state_.right_x = input.right_x;
    state_.right_y = input.right_y;
    return true;
}

runtime::InputState PspInputSystem::State() const noexcept {
    return state_;
}

PspAudioSystem::PspAudioSystem(host::SdlHost &host)
    : host_(host) {}

bool PspAudioSystem::Initialize(const runtime::AudioConfig &config) {
    if (config.channels != 2) return false;
    initialized_ = true;
    return true;
}

void PspAudioSystem::Shutdown() noexcept {
    initialized_ = false;
}

void PspAudioSystem::Queue(std::span<const std::int16_t> interleaved_samples) {
    if (!initialized_ || interleaved_samples.size() < 2) return;
    const std::size_t frames = interleaved_samples.size() / 2;
    host_.queue_audio(interleaved_samples.data(), frames);
}

std::size_t PspAudioSystem::QueuedFrames() const noexcept {
    return initialized_ ? host_.queued_audio_frames() : 0;
}

} // namespace mhf::psp::adapters
