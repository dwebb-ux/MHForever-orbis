#include "memory/MemoryAllocator.h"
#include "memory/MemoryTracker.h"

#include "check.hpp"

#include <array>
#include <cstddef>

int main() {
    std::array<std::byte, 4096> storage{};
    mhf::runtime::LinearFrameAllocator allocator(
        std::span<std::byte>(storage.data(), storage.size()));

    const auto first = allocator.Allocate(64, 64);
    MHF_CHECK(!first.empty());
    MHF_CHECK(reinterpret_cast<std::size_t>(first.data()) % 64 == 0);

    const auto second = allocator.Allocate(128, 16);
    MHF_CHECK(!second.empty());
    MHF_CHECK(allocator.Statistics().allocation_count == 2);
    MHF_CHECK(allocator.Statistics().high_water_mark >= 192);

    allocator.Reset();
    MHF_CHECK(allocator.Statistics().used == 0);
    MHF_CHECK(allocator.Statistics().allocation_count == 0);
    MHF_CHECK(allocator.Statistics().high_water_mark >= 192);
    MHF_CHECK(allocator.Statistics().largest_free_block == storage.size());
    MHF_CHECK(allocator.Allocate(1, 3).empty());
    MHF_CHECK(allocator.Allocate(storage.size() + 1).empty());

    mhf::runtime::MemoryBudget budget;
    budget.SetTotal(1024);
    budget.SetCategory(mhf::runtime::MemoryCategory::Graphics, 512);

    mhf::runtime::MemoryTracker tracker;
    tracker.SetBudget(budget);
    MHF_CHECK(tracker.TryRecord({mhf::runtime::MemoryCategory::Graphics, "textures"}, 400));
    MHF_CHECK(tracker.TryRecord({mhf::runtime::MemoryCategory::Graphics, "buffers"}, 112));
    MHF_CHECK(!tracker.TryRecord({mhf::runtime::MemoryCategory::Graphics, "overflow"}, 1));

    const mhf::runtime::MemoryReport report = tracker.Report();
    MHF_CHECK(report.allocated_bytes == 512);
    MHF_CHECK(report.allocation_count == 2);
    MHF_CHECK(report.categories[static_cast<std::size_t>(mhf::runtime::MemoryCategory::Graphics)].allocated_bytes == 512);

    tracker.Release({mhf::runtime::MemoryCategory::Graphics, "textures"}, 400);
    MHF_CHECK(tracker.CategoryUsage(mhf::runtime::MemoryCategory::Graphics) == 112);
    tracker.Reset();
    MHF_CHECK(tracker.Report().allocated_bytes == 0);
    return mhf::test::finish("runtime_memory_allocator");
}
