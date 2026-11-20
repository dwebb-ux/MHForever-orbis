#include "MemoryTracker.h"

#include <algorithm>
#include <limits>

namespace mhf::runtime {

namespace {

bool ValidCategory(MemoryCategory category) noexcept {
    return MemoryCategoryIndex(category) < kMemoryCategoryCount;
}

bool AddWithoutOverflow(std::size_t left, std::size_t right, std::size_t &out) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    out = left + right;
    return true;
}

} // namespace

void MemoryBudget::SetTotal(std::size_t bytes) noexcept {
    total_ = bytes;
}

void MemoryBudget::SetCategory(MemoryCategory category, std::size_t bytes) noexcept {
    const std::size_t index = MemoryCategoryIndex(category);
    if (index < kMemoryCategoryCount) {
        categories_[index] = bytes;
    }
}

std::size_t MemoryBudget::Total() const noexcept {
    return total_;
}

std::size_t MemoryBudget::Category(MemoryCategory category) const noexcept {
    const std::size_t index = MemoryCategoryIndex(category);
    return index < kMemoryCategoryCount ? categories_[index] : 0;
}

void MemoryTracker::SetBudget(const MemoryBudget &budget) noexcept {
    std::lock_guard lock(mutex_);
    budget_ = budget;
}

MemoryBudget MemoryTracker::Budget() const noexcept {
    std::lock_guard lock(mutex_);
    return budget_;
}

bool MemoryTracker::TryRecord(const AllocationTag &tag, std::size_t bytes) noexcept {
    if (bytes == 0 || !ValidCategory(tag.category)) {
        return false;
    }

    std::lock_guard lock(mutex_);
    const std::size_t index = MemoryCategoryIndex(tag.category);

    std::size_t next_category_usage = 0;
    if (!AddWithoutOverflow(usage_[index], bytes, next_category_usage)) {
        return false;
    }
    std::size_t next_total_usage = 0;
    if (!AddWithoutOverflow(total_usage_, bytes, next_total_usage)) {
        return false;
    }

    const std::size_t category_budget = budget_.Category(tag.category);
    if (category_budget != 0 && next_category_usage > category_budget) {
        return false;
    }
    const std::size_t total_budget = budget_.Total();
    if (total_budget != 0 && next_total_usage > total_budget) {
        return false;
    }

    usage_[index] = next_category_usage;
    total_usage_ = next_total_usage;
    ++allocation_count_[index];
    ++total_allocation_count_;
    category_high_water_[index] = std::max(category_high_water_[index], next_category_usage);
    total_high_water_ = std::max(total_high_water_, next_total_usage);
    return true;
}

void MemoryTracker::Record(const AllocationTag &tag, std::size_t bytes) noexcept {
    (void)TryRecord(tag, bytes);
}

void MemoryTracker::Release(const AllocationTag &tag, std::size_t bytes) noexcept {
    if (bytes == 0 || !ValidCategory(tag.category)) {
        return;
    }

    std::lock_guard lock(mutex_);
    const std::size_t index = MemoryCategoryIndex(tag.category);
    usage_[index] = bytes >= usage_[index] ? 0 : usage_[index] - bytes;
    total_usage_ = bytes >= total_usage_ ? 0 : total_usage_ - bytes;
    if (allocation_count_[index] > 0) {
        --allocation_count_[index];
    }
    if (total_allocation_count_ > 0) {
        --total_allocation_count_;
    }
}

void MemoryTracker::Reset() noexcept {
    std::lock_guard lock(mutex_);
    usage_ = {};
    allocation_count_ = {};
    category_high_water_ = {};
    total_usage_ = 0;
    total_allocation_count_ = 0;
    total_high_water_ = 0;
}

std::size_t MemoryTracker::CategoryUsage(MemoryCategory category) const noexcept {
    if (!ValidCategory(category)) {
        return 0;
    }
    std::lock_guard lock(mutex_);
    return usage_[MemoryCategoryIndex(category)];
}

MemoryReport MemoryTracker::Report() const noexcept {
    std::lock_guard lock(mutex_);

    MemoryReport report;
    report.allocated_bytes = total_usage_;
    report.allocation_count = total_allocation_count_;
    report.high_water_mark = total_high_water_;
    report.budget_bytes = budget_.Total();

    for (std::size_t index = 0; index < kMemoryCategoryCount; ++index) {
        const MemoryCategory category = static_cast<MemoryCategory>(index);
        report.categories[index].category = category;
        report.categories[index].allocated_bytes = usage_[index];
        report.categories[index].allocation_count = allocation_count_[index];
        report.categories[index].high_water_mark = category_high_water_[index];
        report.categories[index].budget_bytes = budget_.Category(category);
    }
    return report;
}

} // namespace mhf::runtime
