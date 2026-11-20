#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace mhf::runtime {

enum class MemoryCategory : std::uint8_t {
    Runtime,
    Graphics,
    Audio,
    Network,
    Streaming,
    Gameplay,
};

inline constexpr std::size_t kMemoryCategoryCount = 6;

inline constexpr std::size_t MemoryCategoryIndex(MemoryCategory category) noexcept {
    return static_cast<std::size_t>(category);
}

struct AllocationTag {
    MemoryCategory category = MemoryCategory::Runtime;
    std::string name;
};

struct MemoryCategoryReport {
    MemoryCategory category = MemoryCategory::Runtime;
    std::size_t allocated_bytes = 0;
    std::size_t allocation_count = 0;
    std::size_t high_water_mark = 0;
    std::size_t budget_bytes = 0;
};

struct MemoryReport {
    std::size_t allocated_bytes = 0;
    std::size_t allocation_count = 0;
    std::size_t high_water_mark = 0;
    std::size_t budget_bytes = 0;
    std::array<MemoryCategoryReport, kMemoryCategoryCount> categories{};
};

class MemoryBudget {
public:
    void SetTotal(std::size_t bytes) noexcept;
    void SetCategory(MemoryCategory category, std::size_t bytes) noexcept;

    [[nodiscard]] std::size_t Total() const noexcept;
    [[nodiscard]] std::size_t Category(MemoryCategory category) const noexcept;

private:
    std::size_t total_ = 0;
    std::array<std::size_t, kMemoryCategoryCount> categories_{};
};

class MemoryTracker {
public:
    void SetBudget(const MemoryBudget &budget) noexcept;
    [[nodiscard]] MemoryBudget Budget() const noexcept;

    [[nodiscard]] bool TryRecord(const AllocationTag &tag, std::size_t bytes) noexcept;
    void Record(const AllocationTag &tag, std::size_t bytes) noexcept;
    void Release(const AllocationTag &tag, std::size_t bytes) noexcept;
    void Reset() noexcept;

    [[nodiscard]] std::size_t CategoryUsage(MemoryCategory category) const noexcept;
    [[nodiscard]] MemoryReport Report() const noexcept;

private:
    mutable std::mutex mutex_;
    MemoryBudget budget_;
    std::array<std::size_t, kMemoryCategoryCount> usage_{};
    std::array<std::size_t, kMemoryCategoryCount> allocation_count_{};
    std::array<std::size_t, kMemoryCategoryCount> category_high_water_{};
    std::size_t total_usage_ = 0;
    std::size_t total_allocation_count_ = 0;
    std::size_t total_high_water_ = 0;
};

} // namespace mhf::runtime
