#include "Ps4FileSystem.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace mhf::platform::ps4 {

namespace {

namespace fs = std::filesystem;

bool IsInsideDataRoot(const fs::path &root, const fs::path &candidate) {
    std::error_code ec;
    const fs::path canonical_root = fs::weakly_canonical(root, ec);
    if (ec) return false;
    const fs::path canonical_candidate = fs::weakly_canonical(candidate, ec);
    if (ec) return false;

    auto root_it = canonical_root.begin();
    auto candidate_it = canonical_candidate.begin();
    for (; root_it != canonical_root.end() && candidate_it != canonical_candidate.end(); ++root_it, ++candidate_it) {
        if (*root_it != *candidate_it) return false;
    }
    return root_it == canonical_root.end();
}

} // namespace

Ps4FileSystem::Ps4FileSystem(std::string_view data_root)
    : data_root_(data_root) {}

std::string Ps4FileSystem::Resolve(std::string_view path) const {
    fs::path requested(path);
    fs::path resolved = requested.is_absolute()
        ? requested
        : fs::path(data_root_) / requested;

    std::error_code ec;
    fs::path canonical_parent = fs::weakly_canonical(resolved.parent_path(), ec);
    if (ec) return {};

    if (!IsInsideDataRoot(fs::path(data_root_), canonical_parent / resolved.filename())) {
        return {};
    }
    return (canonical_parent / resolved.filename()).string();
}

bool Ps4FileSystem::Exists(std::string_view path) {
    const std::string resolved = Resolve(path);
    if (resolved.empty()) return false;
    std::error_code ec;
    return fs::exists(resolved, ec);
}

bool Ps4FileSystem::CreateDirectory(std::string_view path) {
    const std::string resolved = Resolve(path);
    if (resolved.empty()) return false;
    std::error_code ec;
    return fs::create_directories(resolved, ec) || fs::is_directory(resolved, ec);
}

bool Ps4FileSystem::Remove(std::string_view path) {
    const std::string resolved = Resolve(path);
    if (resolved.empty()) return false;
    std::error_code ec;
    return fs::remove_all(resolved, ec) != 0;
}

runtime::FileResult Ps4FileSystem::ReadFile(std::string_view path, std::span<std::byte> destination) {
    const std::string resolved = Resolve(path);
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

runtime::FileResult Ps4FileSystem::WriteFile(std::string_view path, std::span<const std::byte> source) {
    const std::string resolved = Resolve(path);
    if (resolved.empty()) return {runtime::FileError::InvalidArgument, 0};

    fs::path parent = fs::path(resolved).parent_path();
    std::error_code ec;
    fs::create_directories(parent, ec);
    if (ec) return {runtime::FileError::AccessDenied, 0};

    std::ofstream stream(resolved, std::ios::binary | std::ios::trunc);
    if (!stream) return {runtime::FileError::AccessDenied, 0};

    stream.write(reinterpret_cast<const char *>(source.data()), static_cast<std::streamsize>(source.size()));
    if (!stream) return {runtime::FileError::IoFailure, 0};
    return {runtime::FileError::None, source.size()};
}

bool Ps4FileSystem::Rename(std::string_view source, std::string_view destination) {
    const std::string resolved_source = Resolve(source);
    const std::string resolved_destination = Resolve(destination);
    if (resolved_source.empty() || resolved_destination.empty()) return false;
    std::error_code ec;
    fs::rename(resolved_source, resolved_destination, ec);
    return !ec;
}

std::vector<std::string> Ps4FileSystem::ListDirectory(std::string_view path) {
    const std::string resolved = Resolve(path);
    if (resolved.empty()) return {};

    std::vector<std::string> entries;
    std::error_code ec;
    for (const fs::directory_entry &entry : fs::directory_iterator(resolved, ec)) {
        if (ec) break;
        entries.push_back(entry.path().filename().string());
    }
    return entries;
}

} // namespace mhf::platform::ps4
