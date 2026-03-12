#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

class PlatformServices {
public:
    PlatformServices() = default;

    static PlatformServices desktop_default();
    static PlatformServices android(const char *internal_data_path);

    std::filesystem::path session_state_path() const;
    std::vector<std::string> candidate_asset_paths(
        std::string_view relative_asset_path) const;
    std::filesystem::path temp_asset_path(std::string_view asset_path) const;
    bool write_binary_file(
        const std::filesystem::path &path,
        const void *data,
        size_t size) const;

private:
    explicit PlatformServices(
        std::filesystem::path save_root,
        std::filesystem::path temp_root,
        std::vector<std::filesystem::path> asset_roots);

    std::filesystem::path save_root_;
    std::filesystem::path temp_root_;
    std::vector<std::filesystem::path> asset_roots_;
};
