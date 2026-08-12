#include "platform/platform_services.hpp"

#include <array>
#include <fstream>
#include <system_error>
#include <unordered_set>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {
namespace fs = std::filesystem;

fs::path executable_directory() {
#if defined(_WIN32)
    std::array<char, 4096> path{};
    const unsigned long len = GetModuleFileNameA(
        nullptr, path.data(), static_cast<unsigned long>(path.size()));
    if (len > 0 && len < path.size()) {
        return fs::path(std::string(path.data(), len)).parent_path();
    }
    return fs::path(".");
#elif defined(__EMSCRIPTEN__)
    return fs::path("/");
#elif defined(__linux__)
    std::array<char, 4096> path{};
    const ssize_t len = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (len > 0) {
        path[static_cast<size_t>(len)] = '\0';
        return fs::path(path.data()).parent_path();
    }
    return fs::path(".");
#elif defined(__APPLE__)
    std::array<char, 4096> path{};
    uint32_t size = static_cast<uint32_t>(path.size());
    if (_NSGetExecutablePath(path.data(), &size) == 0) {
        return fs::path(path.data()).parent_path();
    }
    return fs::path(".");
#else
    return fs::path(".");
#endif
}

void append_root_with_parents(
    std::vector<fs::path> &roots,
    const fs::path &root,
    int max_depth) {
    fs::path current = root;
    for (int i = 0; i < max_depth; ++i) {
        roots.push_back(current);
        current /= "..";
    }
}

std::vector<fs::path> dedupe_roots(const std::vector<fs::path> &roots) {
    std::vector<fs::path> unique_roots;
    std::unordered_set<std::string> seen;
    for (const fs::path &root : roots) {
        std::error_code ec;
        const fs::path normalized = root.lexically_normal();
        const std::string key = normalized.generic_string();
        if (!key.empty() && seen.insert(key).second) {
            unique_roots.push_back(normalized);
        }
        (void)ec;
    }
    return unique_roots;
}
} // namespace

PlatformServices::PlatformServices(
    fs::path save_root,
    fs::path temp_root,
    std::vector<fs::path> asset_roots)
    : save_root_(std::move(save_root)),
      temp_root_(std::move(temp_root)),
      asset_roots_(dedupe_roots(asset_roots)) {}

PlatformServices PlatformServices::desktop_default() {
    std::vector<fs::path> asset_roots;

    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (!ec) {
        append_root_with_parents(asset_roots, cwd, 6);
    }

    const fs::path exe_dir = executable_directory();
    append_root_with_parents(asset_roots, exe_dir, 6);

    return PlatformServices(
        exe_dir / "save",
        exe_dir / "tmp",
        std::move(asset_roots));
}

PlatformServices PlatformServices::web() {
    return PlatformServices(
        fs::path("/save"),
        fs::path("/tmp"),
        std::vector<fs::path>{
            fs::path("/"),
            fs::path("."),
        });
}

PlatformServices PlatformServices::android(const char *internal_data_path) {
    const fs::path data_root =
        (internal_data_path && *internal_data_path != '\0')
            ? fs::path(internal_data_path)
            : fs::path(".");

    std::vector<fs::path> asset_roots{
        data_root,
        fs::path("."),
        fs::path(".."),
        fs::path("/data/local/tmp/voxov"),
    };

    return PlatformServices(
        data_root / "save",
        data_root,
        std::move(asset_roots));
}

PlatformServices PlatformServices::for_testing(const fs::path &data_root) {
    return PlatformServices(
        data_root / "save",
        data_root / "tmp",
        std::vector<fs::path>{data_root});
}

fs::path PlatformServices::session_state_path() const {
    return save_root_ / "voxov_session_state.bin";
}

std::vector<std::string> PlatformServices::candidate_asset_paths(
    std::string_view relative_asset_path) const {
    std::vector<std::string> out;
    if (relative_asset_path.empty()) {
        return out;
    }

    const fs::path relative(relative_asset_path);
    if (relative.is_absolute()) {
        out.push_back(relative.generic_string());
        return out;
    }

    out.reserve(asset_roots_.size());
    for (const fs::path &root : asset_roots_) {
        out.push_back((root / relative).lexically_normal().generic_string());
    }
    return out;
}

fs::path PlatformServices::temp_asset_path(std::string_view asset_path) const {
    return (temp_root_ / "assets" / fs::path(asset_path)).lexically_normal();
}

bool PlatformServices::write_binary_file(
    const fs::path &path,
    const void *data,
    size_t size) const {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
    out.close();
    return out.good();
}

bool PlatformServices::write_binary_file_atomic(
    const fs::path &path,
    const void *data,
    size_t size) const {
    fs::path temp_path = path;
    temp_path += ".tmp";
    if (!write_binary_file(temp_path, data, size)) {
        return false;
    }

    std::error_code ec;
    fs::rename(temp_path, path, ec);
    if (!ec) {
        return true;
    }

    // Windows does not replace an existing destination with rename. Preserve
    // the previous file until the complete replacement has been written.
    fs::path backup_path = path;
    backup_path += ".bak";
    std::error_code backup_ec;
    if (fs::exists(path, backup_ec) && !backup_ec) {
        fs::remove(backup_path, backup_ec);
        backup_ec.clear();
        fs::rename(path, backup_path, backup_ec);
        if (backup_ec) {
            fs::remove(temp_path, ec);
            return false;
        }
    }

    ec.clear();
    fs::rename(temp_path, path, ec);
    if (ec) {
        std::error_code restore_ec;
        if (fs::exists(backup_path, restore_ec) && !restore_ec) {
            fs::rename(backup_path, path, restore_ec);
        }
        fs::remove(temp_path, restore_ec);
        return false;
    }
    fs::remove(backup_path, ec);
    return true;
}

bool PlatformServices::read_binary_file(
    const fs::path &path,
    std::vector<uint8_t> &out) const {
    out.clear();
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        fs::path backup_path = path;
        backup_path += ".bak";
        input.clear();
        input.open(backup_path, std::ios::binary | std::ios::ate);
        if (!input.is_open()) {
            return false;
        }
    }
    const std::streampos end = input.tellg();
    if (end < 0) {
        return false;
    }
    const size_t size = static_cast<size_t>(end);
    out.resize(size);
    input.seekg(0, std::ios::beg);
    if (size > 0) {
        input.read(reinterpret_cast<char *>(out.data()),
                   static_cast<std::streamsize>(size));
    }
    return input.good();
}
