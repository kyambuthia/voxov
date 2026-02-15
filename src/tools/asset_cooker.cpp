#include "engine_assets/cooked_asset.hpp"

#include <fmt/core.h>

#include <string>
#include <vector>

namespace {

int cook_gltf(const std::string &input, const std::string &output) {
    std::vector<uint8_t> source;
    if (!read_binary_file(input, source)) {
        fmt::print(stderr, "Failed to read glTF input: {}\n", input);
        return 2;
    }

    std::string metadata_text = "importer=tiny-placeholder;animations=hook;materials=hook";
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
    std::string metadata_text;
    if (codec == CookedTextureCodec::Ktx2 || codec == CookedTextureCodec::Basis) {
        metadata_text = "transcode=runtime;preferred=1";
    } else {
        metadata_text = "transcode=runtime;fallback=pngjpg";
    }
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
