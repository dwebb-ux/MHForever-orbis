#include "MemoryAllocator.h"

#include <algorithm>
#include <limits>

namespace mhf::runtime {

namespace {

bool ValidAlignment(std::size_t alignment) noexcept {
    return alignment != 0 && (alignment & (alignment - 1)) == 0;
}

std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept {
    const std::size_t mask = alignment - 1;
    if (value > (std::numeric_limits<std::size_t>::max() - mask)) {
        return std::numeric_limits<std::size_t>::max();
    }
    return (value + mask) & ~mask;
}

} // namespace

LinearFrameAllocator::LinearFrameAllocator(std::span<std::byte> storage) noexcept
    : storage_(storage) {}

void LinearFrameAllocator::Reset() noexcept {
    offset_ = 0;
    allocation_count_ = 0;
}

std::span<std::byte> LinearFrameAllocator::Allocate(std::size_t size, std::size_t alignment) {
    if (size == 0 || !ValidAlignment(alignment)) {
        return {};
    }

    const std::size_t base = reinterpret_cast<std::size_t>(storage_.data());
    if (base > std::numeric_limits<std::size_t>::max() - offset_) {
        return {};
    }
    const std::size_t absolute = AlignUp(base + offset_, alignment);
    if (absolute == std::numeric_limits<std::size_t>::max() || absolute < base) {
        return {};
    }
    const std::size_t aligned_base = absolute - base;
    if (aligned_base > storage_.size() || size > storage_.size() - aligned_base) {
        return {};
    }

    std::byte *result = storage_.data() + aligned_base;
    offset_ = aligned_base + size;
    high_water_mark_ = std::max(high_water_mark_, offset_);
    ++allocation_count_;
    return {result, size};
}

MemoryStatistics LinearFrameAllocator::Statistics() const noexcept {
    MemoryStatistics stats;
    stats.capacity = storage_.size();
    stats.used = offset_;
    stats.allocated_bytes = offset_;
    stats.allocation_count = allocation_count_;
    stats.high_water_mark = high_water_mark_;
    stats.largest_free_block = storage_.size() - offset_;
    return stats;
}

ArenaAllocator::ArenaAllocator(std::byte *storage, std::size_t capacity) noexcept
    : storage_(storage), capacity_(storage == nullptr ? 0 : capacity) {}

void ArenaAllocator::Reset() noexcept {
    offset_ = 0;
    allocation_count_ = 0;
}

std::span<std::byte> ArenaAllocator::Allocate(std::size_t size, std::size_t alignment) {
    if (storage_ == nullptr || size == 0 || !ValidAlignment(alignment)) {
        return {};
    }

    const std::size_t base = reinterpret_cast<std::size_t>(storage_);
    if (base > std::numeric_limits<std::size_t>::max() - offset_) {
        return {};
    }
    const std::size_t absolute = AlignUp(base + offset_, alignment);
    if (absolute == std::numeric_limits<std::size_t>::max() || absolute < base) {
        return {};
    }

    const std::size_t relative = absolute - base;
    if (relative > capacity_ || size > capacity_ - relative) {
        return {};
    }

    std::byte *result = storage_ + relative;
    offset_ = relative + size;
    high_water_mark_ = std::max(high_water_mark_, offset_);
    ++allocation_count_;
    return {result, size};
}

MemoryStatistics ArenaAllocator::Statistics() const noexcept {
    MemoryStatistics stats;
    stats.capacity = capacity_;
    stats.used = offset_;
    stats.allocated_bytes = offset_;
    stats.allocation_count = allocation_count_;
    stats.high_water_mark = high_water_mark_;
    stats.largest_free_block = capacity_ - offset_;
    return stats;
}

} // namespace mhf::runtime
