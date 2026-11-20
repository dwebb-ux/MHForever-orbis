#include "backend_factory.hpp"

#include "gl_backend.hpp"
#include "vulkan_backend.hpp"

namespace mhf::psp::gpu {

std::unique_ptr<Backend> make_backend(const RenderConfig &config) {
    switch (config.backend) {
    case BackendKind::Gl:
        if (config.window == nullptr || config.memory == nullptr) return nullptr;
        return make_gl_backend(config.window, config.scale, *config.memory, config.smooth_textures,
                               config.overlay, config.target_fps, config.live);
    case BackendKind::Vulkan:
        if (config.window == nullptr || config.memory == nullptr) return nullptr;
        return VulkanBackend::Create(config.window, config.vsync, config.scale, config.memory,
                                     config.live, config.overlay);
    }
    return nullptr;
}

} // namespace mhf::psp::gpu
