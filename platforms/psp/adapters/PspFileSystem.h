#pragma once

#include "interfaces/IFileSystem.h"

#include <filesystem>
#include <string>

namespace mhf::psp::adapters {

class PspFileSystem final : public runtime::IFileSystem {
public:
    explicit PspFileSystem(std::filesystem::path root = "memstick");

    bool Exists(std::string_view path) override;
    bool CreateDirectory(std::string_view path) override;
    bool Remove(std::string_view path) override;
    runtime::FileResult ReadFile(std::string_view path, std::span<std::byte> destination) override;
    runtime::FileResult WriteFile(std::string_view path, std::span<const std::byte> source) override;
    bool Rename(std::string_view source, std::string_view destination) override;
    std::vector<std::string> ListDirectory(std::string_view path) override;

private:
    [[nodiscard]] std::filesystem::path Resolve(std::string_view path) const;

    std::filesystem::path root_;
};

} // namespace mhf::psp::adapters
