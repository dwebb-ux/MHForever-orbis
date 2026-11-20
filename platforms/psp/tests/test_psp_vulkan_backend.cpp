#include "backend_factory.hpp"
#include "ge.hpp"
#include "gl_backend.hpp"
#include "host/sdl_host.hpp"

#include <SDL3/SDL.h>

#include <psprecomp/guest_memory.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

namespace {

bool test_software_fallback_from_vulkan_window() {
    mhf::host::SdlHost host;
    mhf::host::HostOptions options;
    options.title = "MHForever Vulkan fallback test";
    options.hidden = true;
    options.scale = 1;
    options.vulkan = true;
    if (!host.open(options)) {
        std::fprintf(stderr, "[Vulkan test] SDL cannot create a Vulkan window: %s\n", SDL_GetError());
        return false;
    }

    constexpr std::array<std::uint32_t, 4> pixels{0xFF202020u, 0xFF404040u, 0xFF606060u, 0xFF808080u};
    host.present(pixels.data(), 2, 2, 2);
    int width = 0, height = 0;
    host.window_pixel_size(width, height);
    return width > 0 && height > 0;
}

} // namespace

namespace {

bool run_commands(psprecomp::GuestMemory &memory, mhf::psp::gpu::Ge &ge,
                  std::span<const std::uint32_t> commands) {
    constexpr std::uint32_t kCommandAddress = 0x08010000u;
    std::uint8_t *destination = memory.raw_pointer(kCommandAddress, commands.size_bytes());
    if (destination == nullptr) return false;
    std::memcpy(destination, commands.data(), commands.size_bytes());
    static_cast<void>(ge.run(kCommandAddress, 0, {}));
    return true;
}

bool set_framebuffer(psprecomp::GuestMemory &memory, mhf::psp::gpu::Ge &ge,
                     std::uint32_t address, std::uint32_t height) {
    const std::array<std::uint32_t, 6> commands{
        (0x9Cu << 24u) | address,
        (0x9Du << 24u) | 512u,
        (0xD2u << 24u) | 3u,
        (0xD4u << 24u),
        (0xD5u << 24u) | ((height - 1u) << 10u) | 479u,
        (0x0Cu << 24u),
    };
    return run_commands(memory, ge, commands);
}

} // namespace

int main() {
    using namespace mhf::psp::gpu;

    RenderConfig invalid_config;
    invalid_config.backend = BackendKind::Vulkan;
    if (make_backend(invalid_config) != nullptr) {
        std::fprintf(stderr, "[Vulkan test] a null SDL window unexpectedly created a backend\n");
        return 1;
    }

    if (!test_software_fallback_from_vulkan_window()) return 77;

    mhf::host::SdlHost host;
    mhf::host::HostOptions options;
    options.title = "MHForever Vulkan clear test";
    options.hidden = true;
    options.scale = 1;
    options.vulkan = true;
    if (!host.open(options)) {
        std::fprintf(stderr, "[Vulkan test] SDL cannot create a Vulkan window: %s\n", SDL_GetError());
        return 77;
    }

    psprecomp::GuestMemory memory;
    mhf::psp::OverlayImage overlay;
    overlay.width = 480;
    overlay.height = 272;
    overlay.visible = true;
    overlay.aspect = 480.0f / 272.0f;
    overlay.pixels.resize(480u * 272u);
    overlay.pixels[100u * 480u + 100u] = 0xFF0000FFu;
    mhf::psp::LiveSettings live;
    live.wide_aspect.store(16.0f / 9.0f, std::memory_order_relaxed);
    RenderConfig config;
    config.backend = BackendKind::Vulkan;
    config.window = static_cast<SDL_Window *>(host.window_handle());
    config.vsync = true;
    config.scale = 2;
    config.memory = &memory;
    config.overlay = &overlay;
    config.live = &live;
    std::unique_ptr<Backend> backend = make_backend(config);
    if (!backend) {
        std::fprintf(stderr, "[Vulkan test] no usable Vulkan surface/device is available\n");
        return 77;
    }

    Ge ge(memory);
    if (!set_framebuffer(memory, ge, 0u, 272u)) {
        std::fprintf(stderr, "[Vulkan test] cannot initialize the guest framebuffer state\n");
        return 1;
    }
    Vertex a{}, b{}, c{};
    a.x = 0.0f;
    a.y = 0.0f;
    b.x = 480.0f;
    b.y = 0.0f;
    c.x = 0.0f;
    c.y = 272.0f;
    for (Vertex *vertex : {&a, &b, &c}) {
        vertex->r = 255.0f;
        vertex->g = 64.0f;
        vertex->b = 32.0f;
        vertex->a = 255.0f;
        vertex->fog = 1.0f;
    }
    backend->triangle(ge, a, b, c);
    b.y = 272.0f;
    backend->sprite(ge, a, b);
    backend->flush();
    backend->present(0x04000000u, 512u, 3u);
    constexpr std::array<std::uint8_t, 4> expected{255u, 64u, 32u, 255u};
    const std::uint8_t *presented = memory.raw_pointer(0x04000000u + (100u * 512u + 100u) * 4u, 4u);
    if (presented == nullptr || std::memcmp(presented, expected.data(), expected.size()) != 0) {
        std::fprintf(stderr, "[Vulkan test] rendered framebuffer was not read back to guest VRAM\n");
        return 1;
    }

    memory.store32(0x04000000u + (100u * 512u + 100u) * 4u, 0u);
    backend->block_transfer(0x04000000u, 512u, 100u, 100u,
                            0x08020000u, 1u, 0u, 0u, 1u, 1u, 4u);
    const std::uint8_t *ram_readback = memory.raw_pointer(0x08020000u, 4u);
    if (ram_readback == nullptr || std::memcmp(ram_readback, expected.data(), expected.size()) != 0) {
        std::fprintf(stderr, "[Vulkan test] framebuffer-to-RAM block-transfer readback failed\n");
        return 1;
    }

    backend->block_transfer(0x04000000u, 512u, 100u, 100u,
                            0x04000000u, 512u, 101u, 100u, 1u, 1u, 4u);
    backend->block_transfer(0x04000000u, 512u, 101u, 100u,
                            0x08020004u, 1u, 0u, 0u, 1u, 1u, 4u);
    ram_readback = memory.raw_pointer(0x08020004u, 4u);
    if (ram_readback == nullptr || std::memcmp(ram_readback, expected.data(), expected.size()) != 0) {
        std::fprintf(stderr, "[Vulkan test] overlapping framebuffer block transfer was not safe\n");
        return 1;
    }

    if (!set_framebuffer(memory, ge, 0x00100000u, 272u)) return 1;
    backend->triangle(ge, a, b, c);
    backend->present(0x04100000u, 512u, 3u);
    backend->block_transfer(0x04000000u, 512u, 100u, 100u,
                            0x04100000u, 512u, 0u, 0u, 1u, 1u, 4u);
    backend->present(0x04100000u, 512u, 3u);
    const std::uint8_t *offscreen_copy = memory.raw_pointer(0x04100000u, 4u);
    if (offscreen_copy == nullptr || std::memcmp(offscreen_copy, expected.data(), expected.size()) != 0) {
        std::fprintf(stderr, "[Vulkan test] offscreen-to-offscreen transfer failed\n");
        return 1;
    }

    if (!set_framebuffer(memory, ge, 0u, 400u)) return 1;
    Vertex resize_a = a;
    Vertex resize_b = b;
    resize_a.x = 300.0f;
    resize_a.y = 200.0f;
    resize_b.x = 320.0f;
    resize_b.y = 220.0f;
    backend->sprite(ge, resize_a, resize_b);
    backend->present(0x04000000u, 512u, 3u);
    presented = memory.raw_pointer(0x04000000u + (100u * 512u + 100u) * 4u, 4u);
    if (presented == nullptr || std::memcmp(presented, expected.data(), expected.size()) != 0) {
        std::fprintf(stderr, "[Vulkan test] render-target growth did not preserve existing pixels\n");
        return 1;
    }
    for (int frame = 0; frame < 3; ++frame) backend->present(0, 0, 0);
    backend.reset();
    return 0;
}
