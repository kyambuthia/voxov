#include "platform/platform_services.hpp"

#include <array>
#include <cstdlib>
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

constexpr const char *kSessionStateFile = "voxov_session_state.bin";

fs::path env_path(const char *name) {
    const char *value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? fs::path(value) : fs::path();
}

// Desktop saves live in the user's data directory, not beside the executable:
// the executable sits in build/, which scripts/clean_build.sh deletes.
// VOXOV_SAVE_DIR overrides the location (useful for tests and profiling runs).
fs::path desktop_save_root(const fs::path &exe_dir, bool &is_override) {
    is_override = false;
    if (fs::path dir = env_path("VOXOV_SAVE_DIR"); !dir.empty()) {
        is_override = true;
        return dir;
    }
#if defined(_WIN32)
    if (const fs::path appdata = env_path("APPDATA"); !appdata.empty()) {
        return appdata / "voxov";
    }
#elif defined(__APPLE__)
    if (const fs::path home = env_path("HOME"); !home.empty()) {
        return home / "Library" / "Application Support" / "voxov";
    }
#elif !defined(__EMSCRIPTEN__)
    if (const fs::path xdg = env_path("XDG_DATA_HOME");
        !xdg.empty() && xdg.is_absolute()) {
        return xdg / "voxov";
    }
    if (const fs::path home = env_path("HOME"); !home.empty()) {
        return home / ".local" / "share" / "voxov";
    }
#endif
    return exe_dir / "save";
}

// Earlier builds saved beside the executable. Copy that save across once so
// existing progress carries over; the old file is left in place.
void migrate_legacy_save(const fs::path &legacy_root, const fs::path &save_root) {
    std::error_code ec;
    const fs::path legacy = legacy_root / kSessionStateFile;
    const fs::path target = save_root / kSessionStateFile;
    if (legacy_root.lexically_normal() == save_root.lexically_normal() ||
        fs::exists(target, ec) || !fs::exists(legacy, ec)) {
        return;
    }
    fs::create_directories(save_root, ec);
    fs::copy_file(legacy, target, ec);
}

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

    bool is_override = false;
    const fs::path save_root = desktop_save_root(exe_dir, is_override);
    if (!is_override) {
        migrate_legacy_save(exe_dir / "save", save_root);
    }

    return PlatformServices(
        save_root,
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
    return save_root_ / kSessionStateFile;
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
