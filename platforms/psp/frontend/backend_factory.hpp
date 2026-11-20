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

namespace gpu {

struct RenderConfig {
    BackendKind backend = BackendKind::Gl;
    SDL_Window *window{};
    int scale = 4;
    psprecomp::GuestMemory *memory{};
    bool smooth_textures{};
    bool vsync = true;
    OverlayImage *overlay{};
    int target_fps = 30;
    LiveSettings *live{};
};

[[nodiscard]] std::unique_ptr<Backend> make_backend(const RenderConfig &config);

} // namespace gpu
} // namespace mhf::psp
