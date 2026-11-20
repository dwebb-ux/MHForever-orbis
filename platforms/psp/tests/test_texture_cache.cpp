#include "texture_cache.hpp"

#include "check.hpp"

#include <array>
#include <cstdint>
#include <vector>

int main() {
    using namespace mhf::psp::textures;

    const TextureCacheKey indexed_a = MakeTextureCacheKey(0x04001000u, 4, 4, 4, 4, false, 1);
    const TextureCacheKey indexed_b = MakeTextureCacheKey(0x04001000u, 4, 4, 4, 4, false, 2);
    MHF_CHECK(indexed_a != indexed_b);
    MHF_CHECK(MakeTextureCacheKey(0x04001000u, 0, 4, 4, 4, false, 1) ==
              MakeTextureCacheKey(0x04001000u, 0, 4, 4, 4, false, 2));

    mhf::psp::font_glyphs::Page page;
    page.width = 6;
    page.height = 6;
    const std::array<std::uint32_t, 16> clut{0xFF010203u};
    std::vector<std::uint32_t> source(36, clut[0]);
    TextPageCache cache;
    TextPageScratch scratch;

    const DirtyRect initial = RebuildTextPage(cache, page, source, clut, 0, {}, scratch);
    MHF_CHECK(initial.full);
    MHF_CHECK(initial.x == 0 && initial.y == 0 && initial.width == 48 && initial.height == 48);
    MHF_CHECK(cache.rgba8.size() == 48u * 48u);
    MHF_CHECK(cache.rgba8.front() == clut[0] && cache.rgba8.back() == clut[0]);

    const DirtyRect unchanged = RebuildTextPage(cache, page, source, clut, 0, {}, scratch);
    MHF_CHECK(unchanged.width == 0 && unchanged.height == 0);

    source[3u * 6u + 3u] = 0xFF123456u;
    const DirtyRect changed = RebuildTextPage(cache, page, source, clut, 0, {}, scratch);
    MHF_CHECK(!changed.full);
    MHF_CHECK(changed.x == 16 && changed.y == 16 && changed.width == 24 && changed.height == 24);
    MHF_CHECK(cache.rgba8[24u * 48u + 24u] == source[21]);
    MHF_CHECK(cache.rgba8[16u * 48u + 16u] == clut[0]);
}
