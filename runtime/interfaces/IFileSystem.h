#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mhf::runtime {

enum class FileError : std::uint8_t {
    None,
    NotFound,
    AccessDenied,
    AlreadyExists,
    InvalidArgument,
    OutOfMemory,
    IoFailure,
};

struct FileResult {
    FileError error = FileError::None;
    std::size_t bytes = 0;
};

class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    virtual bool Exists(std::string_view path) = 0;
    virtual bool CreateDirectory(std::string_view path) = 0;
    virtual bool Remove(std::string_view path) = 0;
    virtual FileResult ReadFile(std::string_view path, std::span<std::byte> destination) = 0;
    virtual FileResult WriteFile(std::string_view path, std::span<const std::byte> source) = 0;
    virtual bool Rename(std::string_view source, std::string_view destination) = 0;
    virtual std::vector<std::string> ListDirectory(std::string_view path) = 0;
};

} // namespace mhf::runtime
