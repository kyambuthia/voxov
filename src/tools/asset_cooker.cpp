#include "engine_assets/cooked_asset.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <string>
#include <vector>

namespace {

uint64_t fnv1a64(const std::vector<uint8_t> &bytes) {
    constexpr uint64_t k_offset = 1469598103934665603ull;
    constexpr uint64_t k_prime = 1099511628211ull;
    uint64_t hash = k_offset;
    for (uint8_t b : bytes) {
        hash ^= static_cast<uint64_t>(b);
        hash *= k_prime;
    }
    return hash;
}

uint32_t count_substr(std::string_view haystack, std::string_view needle) {
    if (needle.empty() || haystack.size() < needle.size()) {
        return 0;
    }
    uint32_t count = 0;
    size_t pos = 0;
    while (true) {
        pos = haystack.find(needle, pos);
        if (pos == std::string_view::npos) {
            break;
        }
        ++count;
        pos += needle.size();
    }
    return count;
}

bool decode_glb_json_chunk(const std::vector<uint8_t> &bytes, std::string &out_json) {
    if (bytes.size() < 20) {
        return false;
    }

    auto read_u32_le = [&](size_t offset) -> uint32_t {
        if (offset + 4 > bytes.size()) {
            return 0;
        }
        uint32_t value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(uint32_t));
        return value;
    };

    const uint32_t magic = read_u32_le(0);
    const uint32_t version = read_u32_le(4);
    if (magic != 0x46546C67u || version != 2u) { // "glTF"
        return false;
    }

    const uint32_t json_length = read_u32_le(12);
    const uint32_t chunk_type = read_u32_le(16);
    if (chunk_type != 0x4E4F534Au || json_length == 0) { // "JSON"
        return false;
    }
    if (20u + json_length > bytes.size()) {
        return false;
    }

    out_json.assign(reinterpret_cast<const char *>(bytes.data() + 20), json_length);
    return true;
}

std::string build_gltf_metadata(const std::string &input, const std::vector<uint8_t> &source) {
    const std::string ext = lowercase_ext(input);
    std::string json_text;
    if (ext == "gltf") {
        json_text.assign(reinterpret_cast<const char *>(source.data()), source.size());
    } else if (ext == "glb") {
        decode_glb_json_chunk(source, json_text);
    }

    uint32_t mesh_markers = 0;
    uint32_t node_markers = 0;
    uint32_t animation_markers = 0;
    if (!json_text.empty()) {
        const std::string_view json_view(json_text);
        mesh_markers = count_substr(json_view, "\"meshes\"");
        node_markers = count_substr(json_view, "\"nodes\"");
        animation_markers = count_substr(json_view, "\"animations\"");
    }

    return fmt::format(
        "importer=minimal-gltf-v1;source_ext={};payload_bytes={};hash_fnv1a64={:016x};mesh_markers={};node_markers={};animation_markers={}",
        ext,
        source.size(),
        fnv1a64(source),
        mesh_markers,
        node_markers,
        animation_markers);
}

std::string build_texture_metadata(
    const std::string &input,
    CookedTextureCodec codec,
    const std::vector<uint8_t> &source) {
    const char *codec_name = "unknown";
    switch (codec) {
    case CookedTextureCodec::Ktx2:
        codec_name = "ktx2";
        break;
    case CookedTextureCodec::Basis:
        codec_name = "basis";
        break;
    case CookedTextureCodec::Png:
        codec_name = "png";
        break;
    case CookedTextureCodec::Jpg:
        codec_name = "jpg";
        break;
    case CookedTextureCodec::Unknown:
    default:
        break;
    }

    const std::string transcode_mode =
        (codec == CookedTextureCodec::Ktx2 || codec == CookedTextureCodec::Basis) ? "preferred" : "fallback";
    return fmt::format(
        "importer=minimal-texture-v1;source_ext={};codec={};transcode={};payload_bytes={};hash_fnv1a64={:016x}",
        lowercase_ext(input),
        codec_name,
        transcode_mode,
        source.size(),
        fnv1a64(source));
}

int cook_gltf(const std::string &input, const std::string &output) {
    std::vector<uint8_t> source;
    if (!read_binary_file(input, source)) {
        fmt::print(stderr, "Failed to read glTF input: {}\n", input);
        return 2;
    }

    std::string metadata_text = build_gltf_metadata(input, source);
    std::vector<uint8_t> metadata(metadata_text.begin(), metadata_text.end());

    CookedAssetHeader header{};
    header.kind = CookedAssetKind::Gltf;
    header.texture_codec = CookedTextureCodec::Unknown;
    header.source_path_len = static_cast<uint32_t>(input.size());
    header.metadata_len = static_cast<uint32_t>(metadata.size());
    header.payload_len = static_cast<uint32_t>(source.size());

    if (!write_cooked_asset(output, header, input, metadata, source)) {
        fmt::print(stderr, "Failed to write cooked glTF asset: {}\n", output);
        return 3;
    }

    fmt::print("Cooked glTF {} -> {} ({} bytes)\n", input, output, source.size());
    return 0;
}

CookedTextureCodec infer_texture_codec(const std::string &input) {
    const std::string ext = lowercase_ext(input);
    if (ext == "ktx2") {
        return CookedTextureCodec::Ktx2;
    }
    if (ext == "basis" || ext == "basisu") {
        return CookedTextureCodec::Basis;
    }
    if (ext == "png") {
        return CookedTextureCodec::Png;
    }
    if (ext == "jpg" || ext == "jpeg") {
        return CookedTextureCodec::Jpg;
    }
    return CookedTextureCodec::Unknown;
}

int cook_texture(const std::string &input, const std::string &output) {
    std::vector<uint8_t> source;
    if (!read_binary_file(input, source)) {
        fmt::print(stderr, "Failed to read texture input: {}\n", input);
        return 2;
    }

    const CookedTextureCodec codec = infer_texture_codec(input);
    std::string metadata_text = build_texture_metadata(input, codec, source);
    std::vector<uint8_t> metadata(metadata_text.begin(), metadata_text.end());

    CookedAssetHeader header{};
    header.kind = CookedAssetKind::Texture;
    header.texture_codec = codec;
    header.source_path_len = static_cast<uint32_t>(input.size());
    header.metadata_len = static_cast<uint32_t>(metadata.size());
    header.payload_len = static_cast<uint32_t>(source.size());

    if (!write_cooked_asset(output, header, input, metadata, source)) {
        fmt::print(stderr, "Failed to write cooked texture asset: {}\n", output);
        return 3;
    }

    fmt::print("Cooked texture {} -> {} ({} bytes)\n", input, output, source.size());
    return 0;
}

int cook_generic(const std::string &input, const std::string &output) {
    std::vector<uint8_t> source;
    if (!read_binary_file(input, source)) {
        fmt::print(stderr, "Failed to read input asset: {}\n", input);
        return 2;
    }

    const std::vector<uint8_t> metadata;

    CookedAssetHeader header{};
    header.kind = CookedAssetKind::Generic;
    header.source_path_len = static_cast<uint32_t>(input.size());
    header.metadata_len = 0;
    header.payload_len = static_cast<uint32_t>(source.size());

    if (!write_cooked_asset(output, header, input, metadata, source)) {
        fmt::print(stderr, "Failed to write cooked asset: {}\n", output);
        return 3;
    }

    fmt::print("Cooked asset {} -> {} ({} bytes)\n", input, output, source.size());
    return 0;
}

}

int main(int argc, char **argv) {
    if (argc < 3) {
        fmt::print("Usage:\n");
        fmt::print("  voxov_asset_cooker <input> <output>\n");
        fmt::print("  voxov_asset_cooker gltf <input.gltf|.glb> <output.vasset>\n");
        fmt::print("  voxov_asset_cooker texture <input.ktx2|basis|png|jpg> <output.vtex>\n");
        return 1;
    }

    if (argc >= 4) {
        const std::string mode = argv[1];
        const std::string input = argv[2];
        const std::string output = argv[3];

        if (mode == "gltf") {
            return cook_gltf(input, output);
        }
        if (mode == "texture") {
            return cook_texture(input, output);
        }
    }

    const std::string input = argv[1];
    const std::string output = argv[2];
    return cook_generic(input, output);
}
