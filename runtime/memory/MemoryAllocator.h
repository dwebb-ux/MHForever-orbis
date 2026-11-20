#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace mhf::runtime {

struct MemoryStatistics {
    std::size_t capacity = 0;
    std::size_t used = 0;
    std::size_t allocated_bytes = 0;
    std::size_t allocation_count = 0;
    std::size_t high_water_mark = 0;
    std::size_t largest_free_block = 0;
};

class IFrameAllocator {
public:
    virtual ~IFrameAllocator() = default;
    virtual void Reset() noexcept = 0;
    virtual std::span<std::byte> Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) = 0;
    [[nodiscard]] virtual MemoryStatistics Statistics() const noexcept = 0;
};

class LinearFrameAllocator final : public IFrameAllocator {
public:
    explicit LinearFrameAllocator(std::span<std::byte> storage) noexcept;

    void Reset() noexcept override;
    std::span<std::byte> Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) override;
    [[nodiscard]] MemoryStatistics Statistics() const noexcept override;

private:
    std::span<std::byte> storage_;
    std::size_t offset_ = 0;
    std::size_t high_water_mark_ = 0;
    std::size_t allocation_count_ = 0;
};

class ArenaAllocator {
public:
    ArenaAllocator(std::byte *storage, std::size_t capacity) noexcept;

    [[nodiscard]] std::span<std::byte> Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));
    void Reset() noexcept;
    [[nodiscard]] MemoryStatistics Statistics() const noexcept;

private:
    std::byte *storage_;
    std::size_t capacity_;
    std::size_t offset_ = 0;
    std::size_t high_water_mark_ = 0;
    std::size_t allocation_count_ = 0;
};

} // namespace mhf::runtime
