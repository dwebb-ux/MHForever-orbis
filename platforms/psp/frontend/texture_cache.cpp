#include "texture_cache.hpp"

#include "ge.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mhf::psp::textures {

TextureCacheKey MakeTextureCacheKey(std::uint32_t address, std::uint32_t format,
                                    std::uint32_t width, std::uint32_t height,
                                    std::uint32_t stride, bool swizzled,
                                    std::uint32_t clut_format) {
    std::uint64_t key = address;
    key = key * 1315423911u ^ (static_cast<std::uint64_t>(format) << 40u) ^
          (static_cast<std::uint64_t>(width) << 20u) ^ height ^
          (static_cast<std::uint64_t>(stride) << 48u) ^ (swizzled ? 0x8000000000000000ull : 0u);
    if (format >= 4 && format <= 7) key ^= static_cast<std::uint64_t>(clut_format) * 0x9E3779B97F4A7C15ull;
    return {key};
}

void DecodeGuestTexture(const gpu::Ge &ge, const TextureMetadata &metadata, DecodedTexture &texture) {
    texture.metadata = metadata;
    texture.rgba8.resize(static_cast<std::size_t>(metadata.width) * metadata.height);
    if (texture.rgba8.empty()) return;
    ge.decode_texture(texture.rgba8.data());
}

DirtyRect RebuildTextPage(TextPageCache &cache, const font_glyphs::Page &page,
                          std::span<const std::uint32_t> source,
                          const std::array<std::uint32_t, 16> &clut,
                          std::uint64_t sheets_generation,
                          const GlyphResolver &resolve_glyph,
                          TextPageScratch &scratch) {
    constexpr int kScale = font_glyphs::kScale;
    const int width = page.width;
    const int height = page.height;
    if (width <= 0 || height <= 0 ||
        source.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return {};
    }

    const std::uint32_t background = clut[0];
    int rx0 = 0, ry0 = 0, rx1 = width, ry1 = height;
    const bool whole = !cache.initialized || cache.width != width || cache.height != height ||
                       cache.clut != clut || cache.source.size() != source.size() ||
                       cache.sheets_generation != sheets_generation;
    if (!whole) {
        rx0 = width;
        ry0 = height;
        rx1 = 0;
        ry1 = 0;
        for (int y = 0; y < height; ++y) {
            const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
            for (int x = 0; x < width; ++x) {
                const std::size_t index = row + static_cast<std::size_t>(x);
                if (source[index] == cache.source[index]) continue;
                rx0 = std::min(rx0, x);
                rx1 = std::max(rx1, x + 1);
                ry0 = std::min(ry0, y);
                ry1 = y + 1;
            }
        }
        if (rx1 <= rx0) return {};
        rx0 = std::max(0, rx0 - 1);
        ry0 = std::max(0, ry0 - 1);
        rx1 = std::min(width, rx1 + 1);
        ry1 = std::min(height, ry1 + 1);
    }

    const int hd_width = width * kScale;
    const int hd_height = height * kScale;
    if (whole) {
        cache.width = width;
        cache.height = height;
        cache.clut = clut;
        cache.sheets_generation = sheets_generation;
        cache.rgba8.assign(static_cast<std::size_t>(hd_width) * static_cast<std::size_t>(hd_height), background);
        cache.initialized = true;
    }
    cache.source.assign(source.begin(), source.end());

    const int hx0 = rx0 * kScale, hy0 = ry0 * kScale;
    const int hx1 = rx1 * kScale, hy1 = ry1 * kScale;
    const int rect_width = hx1 - hx0, rect_height = hy1 - hy0;
    scratch.coverage.assign(static_cast<std::size_t>(rect_width) * static_cast<std::size_t>(rect_height), 0);

    for (int y = hy0; y < hy1; ++y) {
        const std::size_t source_row = static_cast<std::size_t>(y / kScale) * static_cast<std::size_t>(width);
        const std::size_t output_row = static_cast<std::size_t>(y) * static_cast<std::size_t>(hd_width);
        for (int x = hx0; x < hx1; ++x) {
            cache.rgba8[output_row + static_cast<std::size_t>(x)] =
                source[source_row + static_cast<std::size_t>(x / kScale)];
        }
    }

    scratch.kept_glyphs.clear();
    for (const font_glyphs::Glyph &glyph : page.glyphs) {
        const int x0 = std::max({0, glyph.x, glyph.clip_x0});
        const int y0 = std::max({0, glyph.y, glyph.clip_y0});
        const int x1 = std::min({width, glyph.x + glyph.width, glyph.clip_x1});
        const int y1 = std::min({height, glyph.y + glyph.height, glyph.clip_y1});
        if (x1 <= rx0 || x0 >= rx1 || y1 <= ry0 || y0 >= ry1) continue;

        bool present = false;
        for (int y = y0; y < y1 && !present; ++y) {
            const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
            for (int x = x0; x < x1; ++x) {
                if (source[row + static_cast<std::size_t>(x)] != background) {
                    present = true;
                    break;
                }
            }
        }
        if (!present) continue;
        scratch.kept_glyphs.push_back(&glyph);

        const int bx0 = std::max(x0, rx0) * kScale;
        const int bx1 = std::min(x1, rx1) * kScale;
        for (int y = std::max(y0, ry0) * kScale; y < std::min(y1, ry1) * kScale; ++y) {
            const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(hd_width);
            std::fill_n(cache.rgba8.data() + row + static_cast<std::size_t>(bx0),
                        static_cast<std::size_t>(bx1 - bx0), background);
        }
    }

    const auto put_coverage = [&](int x, int y, std::uint8_t value, int clip_x0, int clip_y0,
                                  int clip_x1, int clip_y1) {
        if (value == 0 || x < clip_x0 || y < clip_y0 || x >= clip_x1 || y >= clip_y1) return;
        std::uint8_t &pixel =
            scratch.coverage[static_cast<std::size_t>(y - hy0) * static_cast<std::size_t>(rect_width) +
                             static_cast<std::size_t>(x - hx0)];
        pixel = std::max(pixel, value);
    };

    for (const font_glyphs::Glyph *glyph : scratch.kept_glyphs) {
        const int cx0 = std::max(std::max(0, glyph->clip_x0) * kScale, hx0);
        const int cy0 = std::max(std::max(0, glyph->clip_y0) * kScale, hy0);
        const int cx1 = std::min(std::min(width, glyph->clip_x1) * kScale, hx1);
        const int cy1 = std::min(std::min(height, glyph->clip_y1) * kScale, hy1);
        std::shared_ptr<const font_glyphs::Coverage> image =
            resolve_glyph ? resolve_glyph(*glyph) : nullptr;
        const bool from_sheet = image != nullptr;
        if (!image) image = glyph->hd;
        const int image_x = from_sheet
                                ? static_cast<int>(std::lround(static_cast<double>(glyph->pen_x64) * kScale / 64.0)) +
                                      glyph->box_x * kScale
                                : glyph->hd_x;
        const int image_y = from_sheet
                                ? static_cast<int>(std::lround(static_cast<double>(glyph->pen_y64) * kScale / 64.0)) +
                                      glyph->box_y * kScale
                                : glyph->hd_y;
        if (image && image->width > 0 && image->height > 0) {
            for (int y = std::max(0, cy0 - image_y);
                 y < std::min(image->height, cy1 - image_y); ++y) {
                for (int x = std::max(0, cx0 - image_x);
                     x < std::min(image->width, cx1 - image_x); ++x) {
                    put_coverage(image_x + x, image_y + y,
                                 image->values[static_cast<std::size_t>(y) *
                                                   static_cast<std::size_t>(image->width) +
                                               static_cast<std::size_t>(x)],
                                 cx0, cy0, cx1, cy1);
                }
            }
        }
    }

    for (int y = 0; y < rect_height; ++y) {
        const std::size_t coverage_row = static_cast<std::size_t>(y) * static_cast<std::size_t>(rect_width);
        const std::size_t output_row = static_cast<std::size_t>(y + hy0) * static_cast<std::size_t>(hd_width) +
                                       static_cast<std::size_t>(hx0);
        for (int x = 0; x < rect_width; ++x) {
            const std::uint8_t alpha = scratch.coverage[coverage_row + static_cast<std::size_t>(x)];
            if (alpha == 0) continue;
            const float level = static_cast<float>(alpha) * 15.0f / 255.0f;
            const int lower = std::min(14, static_cast<int>(level));
            const float fraction = level - static_cast<float>(lower);
            std::uint32_t colour = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                const float a = static_cast<float>((clut[static_cast<std::size_t>(lower)] >> shift) & 0xFFu);
                const float b = static_cast<float>((clut[static_cast<std::size_t>(lower) + 1u] >> shift) & 0xFFu);
                colour |= static_cast<std::uint32_t>(std::lround(a + (b - a) * fraction)) << shift;
            }
            cache.rgba8[output_row + static_cast<std::size_t>(x)] = colour;
        }
    }

    return {hx0, hy0, rect_width, rect_height, whole};
}

} // namespace mhf::psp::textures
