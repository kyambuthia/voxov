#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class CookedAssetKind : uint32_t {
    Generic = 0,
    Gltf = 1,
    Texture = 2
};

enum class CookedTextureCodec : uint32_t {
    Unknown = 0,
    Ktx2 = 1,
    Basis = 2,
    Png = 3,
    Jpg = 4
};

struct CookedAssetHeader {
    uint32_t magic = 0x30585656; // VVX0
    uint32_t version = 1;
    CookedAssetKind kind = CookedAssetKind::Generic;
    CookedTextureCodec texture_codec = CookedTextureCodec::Unknown;
    uint32_t source_path_len = 0;
    uint32_t metadata_len = 0;
    uint32_t payload_len = 0;
};

bool read_binary_file(const std::string &path, std::vector<uint8_t> &data);
bool write_cooked_asset(
    const std::string &path,
    const CookedAssetHeader &header,
    const std::string &source_path,
    const std::vector<uint8_t> &metadata,
    const std::vector<uint8_t> &payload);
std::string lowercase_ext(const std::string &path);
