#pragma once

#include "backend.hpp"

#include <memory>

struct SDL_Window;

namespace psprecomp {
class GuestMemory;
}

namespace mhf::psp {
struct LiveSettings;
struct OverlayImage;
}

namespace mhf::psp::gpu {

class VulkanBackend final : public Backend {
public:
    [[nodiscard]] static std::unique_ptr<VulkanBackend> Create(SDL_Window *window, bool vsync, int scale,
                                                               psprecomp::GuestMemory *memory,
                                                               LiveSettings *live = nullptr,
                                                               OverlayImage *overlay = nullptr);
    ~VulkanBackend() override;

    VulkanBackend(const VulkanBackend &) = delete;
    VulkanBackend &operator=(const VulkanBackend &) = delete;

    void triangle(const Ge &ge, const Vertex &a, const Vertex &b, const Vertex &c) override;
    void triangle_unclipped(const Ge &ge, const Vertex &a, const Vertex &b, const Vertex &c) override;
    void sprite(const Ge &ge, const Vertex &a, const Vertex &b) override;
    void block_transfer(std::uint32_t src, std::uint32_t src_stride, std::uint32_t sx, std::uint32_t sy,
                        std::uint32_t dst, std::uint32_t dst_stride, std::uint32_t dx, std::uint32_t dy,
                        std::uint32_t width, std::uint32_t height, std::uint32_t bytes_per_pixel) override;
    void flush() override;
    void present(std::uint32_t address, std::uint32_t stride, std::uint32_t pixel_format) override;

private:
    struct Impl;
    explicit VulkanBackend(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace mhf::psp::gpu
