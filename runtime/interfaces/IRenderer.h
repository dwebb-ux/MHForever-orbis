#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace mhf::runtime {

struct RenderConfig {
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    bool vsync = true;
};

using TextureHandle = std::uint32_t;
inline constexpr TextureHandle kInvalidTexture = 0;

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool Initialize(const RenderConfig &config) = 0;
    virtual void Shutdown() noexcept = 0;
    virtual void BeginFrame() = 0;
    virtual void Submit(std::span<const std::byte> commands) = 0;
    virtual void EndFrame() = 0;
};

} // namespace mhf::runtime
