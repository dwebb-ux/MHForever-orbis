#pragma once

#include "mhf/psp/font_glyphs.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace mhf::psp::gpu {
class Ge;
}

namespace mhf::psp::textures {

struct TextureCacheKey {
    std::uint64_t value{};
    friend bool operator==(const TextureCacheKey &, const TextureCacheKey &) = default;
};

struct TextureMetadata {
    std::uint32_t width{};
    std::uint32_t height{};
    bool clamp_s{};
    bool clamp_t{};
    bool linear_filter{};
};

struct DecodedTexture {
    TextureMetadata metadata;
    std::vector<std::uint32_t> rgba8;
};

[[nodiscard]] TextureCacheKey MakeTextureCacheKey(std::uint32_t address, std::uint32_t format,
                                                  std::uint32_t width, std::uint32_t height,
                                                  std::uint32_t stride, bool swizzled,
                                                  std::uint32_t clut_format);
void DecodeGuestTexture(const gpu::Ge &ge, const TextureMetadata &metadata, DecodedTexture &texture);

struct TextPageCache {
    int width{};
    int height{};
    std::array<std::uint32_t, 16> clut{};
    std::vector<std::uint32_t> source;
    std::vector<std::uint32_t> rgba8;
    std::uint64_t sheets_generation{};
    bool initialized{};
};

struct DirtyRect {
    int x{};
    int y{};
    int width{};
    int height{};
    bool full{};
};

struct TextPageScratch {
    std::vector<std::uint8_t> coverage;
    std::vector<const font_glyphs::Glyph *> kept_glyphs;
};

using GlyphResolver =
    std::function<std::shared_ptr<const font_glyphs::Coverage>(const font_glyphs::Glyph &)>;

[[nodiscard]] DirtyRect RebuildTextPage(TextPageCache &cache, const font_glyphs::Page &page,
                                        std::span<const std::uint32_t> source,
                                        const std::array<std::uint32_t, 16> &clut,
                                        std::uint64_t sheets_generation,
                                        const GlyphResolver &resolve_glyph,
                                        TextPageScratch &scratch);

} // namespace mhf::psp::textures
