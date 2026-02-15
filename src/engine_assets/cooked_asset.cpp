#include "engine_assets/cooked_asset.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

bool read_binary_file(const std::string &path, std::vector<uint8_t> &data) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        return false;
    }

    const std::streamsize size = in.tellg();
    if (size < 0) {
        return false;
    }

    data.resize(static_cast<size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char *>(data.data()), size);
    return true;
}

bool write_cooked_asset(
    const std::string &path,
    const CookedAssetHeader &header,
    const std::string &source_path,
    const std::vector<uint8_t> &metadata,
    const std::vector<uint8_t> &payload) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }

    out.write(reinterpret_cast<const char *>(&header), sizeof(header));
    out.write(source_path.data(), static_cast<std::streamsize>(source_path.size()));
    out.write(reinterpret_cast<const char *>(metadata.data()), static_cast<std::streamsize>(metadata.size()));
    out.write(reinterpret_cast<const char *>(payload.data()), static_cast<std::streamsize>(payload.size()));
    return true;
}

std::string lowercase_ext(const std::string &path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return std::string();
    }

    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}
