#pragma once

#include "interfaces/IFileSystem.h"

#include <string>

namespace mhf::platform::ps4 {

class Ps4FileSystem final : public runtime::IFileSystem {
public:
    explicit Ps4FileSystem(std::string_view data_root = "/data/mhforever");

    bool Exists(std::string_view path) override;
    bool CreateDirectory(std::string_view path) override;
    bool Remove(std::string_view path) override;
    runtime::FileResult ReadFile(std::string_view path, std::span<std::byte> destination) override;
    runtime::FileResult WriteFile(std::string_view path, std::span<const std::byte> source) override;
    bool Rename(std::string_view source, std::string_view destination) override;
    std::vector<std::string> ListDirectory(std::string_view path) override;

private:
    [[nodiscard]] std::string Resolve(std::string_view path) const;

    std::string data_root_;
};

} // namespace mhf::platform::ps4
