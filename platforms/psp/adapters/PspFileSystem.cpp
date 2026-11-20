#include "PspFileSystem.h"

#include <fstream>
#include <system_error>

namespace mhf::psp::adapters {

namespace {

bool IsInside(const std::filesystem::path &root, const std::filesystem::path &candidate) {
    std::error_code ec;
    const auto canonical_root = std::filesystem::weakly_canonical(root, ec);
    if (ec) return false;
    const auto canonical_candidate = std::filesystem::weakly_canonical(candidate, ec);
    if (ec) return false;

    auto root_it = canonical_root.begin();
    auto candidate_it = canonical_candidate.begin();
    for (; root_it != canonical_root.end() && candidate_it != canonical_candidate.end(); ++root_it, ++candidate_it) {
        if (*root_it != *candidate_it) return false;
    }
    return root_it == canonical_root.end();
}

} // namespace

PspFileSystem::PspFileSystem(std::filesystem::path root)
    : root_(std::move(root)) {}

std::filesystem::path PspFileSystem::Resolve(std::string_view path) const {
    std::filesystem::path requested(path);
    if (requested.is_absolute()) return {};

    const std::filesystem::path candidate = root_ / requested;
    return IsInside(root_, candidate) ? candidate : std::filesystem::path{};
}

bool PspFileSystem::Exists(std::string_view path) {
    const auto resolved = Resolve(path);
    if (resolved.empty()) return false;
    std::error_code ec;
    return std::filesystem::exists(resolved, ec);
}

bool PspFileSystem::CreateDirectory(std::string_view path) {
    const auto resolved = Resolve(path);
    if (resolved.empty()) return false;
    std::error_code ec;
    return std::filesystem::create_directories(resolved, ec) ||
           std::filesystem::is_directory(resolved, ec);
}

bool PspFileSystem::Remove(std::string_view path) {
    const auto resolved = Resolve(path);
    if (resolved.empty()) return false;
    std::error_code ec;
    return std::filesystem::remove_all(resolved, ec) != 0;
}

runtime::FileResult PspFileSystem::ReadFile(std::string_view path, std::span<std::byte> destination) {
    const auto resolved = Resolve(path);
    if (resolved.empty()) return {runtime::FileError::InvalidArgument, 0};

    std::ifstream stream(resolved, std::ios::binary);
    if (!stream) return {runtime::FileError::NotFound, 0};
    stream.read(reinterpret_cast<char *>(destination.data()), static_cast<std::streamsize>(destination.size()));
    const std::streamsize count = stream.gcount();
    if (count < 0 || (stream.fail() && !stream.eof())) {
        return {runtime::FileError::IoFailure, 0};
    }
    return {runtime::FileError::None, static_cast<std::size_t>(count)};
}

runtime::FileResult PspFileSystem::WriteFile(std::string_view path, std::span<const std::byte> source) {
    const auto resolved = Resolve(path);
    if (resolved.empty()) return {runtime::FileError::InvalidArgument, 0};

    std::error_code ec;
    std::filesystem::create_directories(resolved.parent_path(), ec);
    if (ec) return {runtime::FileError::AccessDenied, 0};

    std::ofstream stream(resolved, std::ios::binary | std::ios::trunc);
    if (!stream) return {runtime::FileError::AccessDenied, 0};
    stream.write(reinterpret_cast<const char *>(source.data()), static_cast<std::streamsize>(source.size()));
    if (!stream) return {runtime::FileError::IoFailure, 0};
    return {runtime::FileError::None, source.size()};
}

bool PspFileSystem::Rename(std::string_view source, std::string_view destination) {
    const auto resolved_source = Resolve(source);
    const auto resolved_destination = Resolve(destination);
    if (resolved_source.empty() || resolved_destination.empty()) return false;
    std::error_code ec;
    std::filesystem::rename(resolved_source, resolved_destination, ec);
    return !ec;
}

std::vector<std::string> PspFileSystem::ListDirectory(std::string_view path) {
    const auto resolved = Resolve(path);
    if (resolved.empty()) return {};

    std::vector<std::string> entries;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(resolved, ec)) {
        if (ec) break;
        entries.push_back(entry.path().filename().string());
    }
    return entries;
}

} // namespace mhf::psp::adapters
