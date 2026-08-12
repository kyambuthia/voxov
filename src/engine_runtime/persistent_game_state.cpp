#include "engine_runtime/persistent_game_state.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cmath>
#include <type_traits>
#include <utility>

namespace {
constexpr std::array<uint8_t, 8> kMagic{
    'V', 'O', 'X', 'O', 'V', 'S', 'A', 'V'};
constexpr size_t kHeaderSize = 20;
constexpr uint32_t kMaxBlockEdits = 1'000'000;

uint32_t checksum(const uint8_t *data, size_t size) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

template <typename T>
void write_unsigned(std::vector<uint8_t> &out, T value) {
    static_assert(std::is_unsigned_v<T>);
    for (size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(static_cast<uint8_t>(value & 0xffu));
        value >>= 8u;
    }
}

void write_i32(std::vector<uint8_t> &out, int32_t value) {
    write_unsigned(out, std::bit_cast<uint32_t>(value));
}

void write_f64(std::vector<uint8_t> &out, double value) {
    write_unsigned(out, std::bit_cast<uint64_t>(value));
}

template <typename T>
bool read_unsigned(const std::vector<uint8_t> &bytes, size_t &offset, T &out) {
    static_assert(std::is_unsigned_v<T>);
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) {
        return false;
    }
    T value = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<T>(bytes[offset + i]) << (i * 8u);
    }
    offset += sizeof(T);
    out = value;
    return true;
}

bool read_i32(const std::vector<uint8_t> &bytes, size_t &offset,
              int32_t &out) {
    uint32_t value = 0;
    if (!read_unsigned(bytes, offset, value)) {
        return false;
    }
    out = std::bit_cast<int32_t>(value);
    return true;
}

bool read_f64(const std::vector<uint8_t> &bytes, size_t &offset, double &out) {
    uint64_t value = 0;
    if (!read_unsigned(bytes, offset, value)) {
        return false;
    }
    out = std::bit_cast<double>(value);
    return true;
}

bool supported_body(int32_t body_index) {
    return body_index == 1 || body_index == 3;
}

bool valid_edit(const PersistentBlockEdit &edit) {
    const int32_t sector = static_cast<int32_t>(edit.address.sector);
    const int32_t material = static_cast<int32_t>(edit.material);
    return supported_body(edit.body_index) && sector >= 0 && sector < 6 &&
        edit.address.shell >= 0 && material >= 0 &&
        material <= static_cast<int32_t>(VoxelMaterial::Stone);
}
} // namespace

bool encode_persistent_game_state(
    const PersistentGameState &state,
    std::vector<uint8_t> &out,
    std::string &out_error) {
    out.clear();
    out_error.clear();
    if (!supported_body(state.active_body_index)) {
        out_error = "unsupported active body";
        return false;
    }
    const bool finite_position =
        std::isfinite(state.player_local_position.x) &&
        std::isfinite(state.player_local_position.y) &&
        std::isfinite(state.player_local_position.z);
    if (!finite_position ||
        glm::length(state.player_local_position) > 1.0e9) {
        out_error = "invalid player position";
        return false;
    }
    if (state.expedition_stage < ExpeditionStage::CollectSample ||
        state.expedition_stage > ExpeditionStage::Complete) {
        out_error = "invalid expedition stage";
        return false;
    }
    if (state.character < GuiMenu::Character::Humanoid ||
        state.character > GuiMenu::Character::Skeleton) {
        out_error = "invalid character";
        return false;
    }
    if (state.block_edits.size() > kMaxBlockEdits) {
        out_error = "too many block edits";
        return false;
    }
    for (const PersistentBlockEdit &edit : state.block_edits) {
        if (!valid_edit(edit)) {
            out_error = "invalid block edit";
            return false;
        }
    }

    std::vector<uint8_t> payload;
    payload.reserve(32 + state.block_edits.size() * 40);
    write_unsigned(payload, static_cast<uint8_t>(state.expedition_stage));
    write_unsigned(payload, static_cast<uint8_t>(state.character));
    write_unsigned(payload, uint16_t{0});
    write_i32(payload, state.active_body_index);
    write_f64(payload, state.player_local_position.x);
    write_f64(payload, state.player_local_position.y);
    write_f64(payload, state.player_local_position.z);
    write_unsigned(payload, static_cast<uint32_t>(state.block_edits.size()));
    for (const PersistentBlockEdit &edit : state.block_edits) {
        write_i32(payload, edit.body_index);
        write_unsigned(payload, static_cast<uint8_t>(edit.address.sector));
        write_unsigned(payload, static_cast<uint8_t>(edit.material));
        write_unsigned(payload, static_cast<uint8_t>(edit.solid ? 1 : 0));
        write_unsigned(payload, uint8_t{0});
        write_i32(payload, edit.address.shell);
        write_i32(payload, edit.address.chunk.x);
        write_i32(payload, edit.address.chunk.y);
        write_i32(payload, edit.address.chunk.z);
        write_i32(payload, edit.address.block.x);
        write_i32(payload, edit.address.block.y);
        write_i32(payload, edit.address.block.z);
    }

    out.insert(out.end(), kMagic.begin(), kMagic.end());
    write_unsigned(out, PersistentGameState::kFormatVersion);
    write_unsigned(out, PersistentGameState::kGeneratorVersion);
    write_unsigned(out, static_cast<uint32_t>(payload.size()));
    write_unsigned(out, checksum(payload.data(), payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return true;
}

bool decode_persistent_game_state(
    const std::vector<uint8_t> &bytes,
    PersistentGameState &out,
    std::string &out_error) {
    out_error.clear();
    if (bytes.size() < kHeaderSize ||
        !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        out_error = "invalid save header";
        return false;
    }

    size_t offset = kMagic.size();
    uint16_t format_version = 0;
    uint16_t generator_version = 0;
    uint32_t payload_size = 0;
    uint32_t expected_checksum = 0;
    if (!read_unsigned(bytes, offset, format_version) ||
        !read_unsigned(bytes, offset, generator_version) ||
        !read_unsigned(bytes, offset, payload_size) ||
        !read_unsigned(bytes, offset, expected_checksum)) {
        out_error = "truncated save header";
        return false;
    }
    if (format_version != PersistentGameState::kFormatVersion) {
        out_error = "unsupported save format";
        return false;
    }
    if (generator_version != PersistentGameState::kGeneratorVersion) {
        out_error = "incompatible world generator";
        return false;
    }
    if (payload_size != bytes.size() - kHeaderSize ||
        checksum(bytes.data() + kHeaderSize, payload_size) !=
            expected_checksum) {
        out_error = "save payload is truncated or corrupt";
        return false;
    }

    uint8_t stage = 0;
    uint8_t character = 0;
    uint16_t reserved = 0;
    PersistentGameState decoded{};
    if (!read_unsigned(bytes, offset, stage) ||
        !read_unsigned(bytes, offset, character) ||
        !read_unsigned(bytes, offset, reserved) ||
        !read_i32(bytes, offset, decoded.active_body_index) ||
        !read_f64(bytes, offset, decoded.player_local_position.x) ||
        !read_f64(bytes, offset, decoded.player_local_position.y) ||
        !read_f64(bytes, offset, decoded.player_local_position.z)) {
        out_error = "truncated save state";
        return false;
    }
    (void)reserved;
    decoded.expedition_stage = static_cast<ExpeditionStage>(stage);
    decoded.character = static_cast<GuiMenu::Character>(character);

    uint32_t edit_count = 0;
    if (!read_unsigned(bytes, offset, edit_count) ||
        edit_count > kMaxBlockEdits) {
        out_error = "invalid block edit count";
        return false;
    }
    decoded.block_edits.reserve(edit_count);
    for (uint32_t i = 0; i < edit_count; ++i) {
        PersistentBlockEdit edit{};
        uint8_t sector = 0;
        uint8_t material = 0;
        uint8_t solid = 0;
        uint8_t edit_reserved = 0;
        if (!read_i32(bytes, offset, edit.body_index) ||
            !read_unsigned(bytes, offset, sector) ||
            !read_unsigned(bytes, offset, material) ||
            !read_unsigned(bytes, offset, solid) ||
            !read_unsigned(bytes, offset, edit_reserved) ||
            !read_i32(bytes, offset, edit.address.shell) ||
            !read_i32(bytes, offset, edit.address.chunk.x) ||
            !read_i32(bytes, offset, edit.address.chunk.y) ||
            !read_i32(bytes, offset, edit.address.chunk.z) ||
            !read_i32(bytes, offset, edit.address.block.x) ||
            !read_i32(bytes, offset, edit.address.block.y) ||
            !read_i32(bytes, offset, edit.address.block.z)) {
            out_error = "truncated block edit";
            return false;
        }
        (void)edit_reserved;
        edit.address.sector = static_cast<PlanetFace>(sector);
        edit.material = static_cast<VoxelMaterial>(material);
        edit.solid = solid != 0;
        if (solid > 1 || !valid_edit(edit)) {
            out_error = "invalid block edit";
            return false;
        }
        decoded.block_edits.push_back(edit);
    }

    if (offset != bytes.size()) {
        out_error = "unexpected trailing save data";
        return false;
    }
    std::vector<uint8_t> validation;
    if (!encode_persistent_game_state(decoded, validation, out_error)) {
        return false;
    }
    out = std::move(decoded);
    return true;
}

bool save_persistent_game_state(
    const PlatformServices &platform,
    const PersistentGameState &state,
    std::string &out_error) {
    std::vector<uint8_t> bytes;
    if (!encode_persistent_game_state(state, bytes, out_error)) {
        return false;
    }
    if (!platform.write_binary_file_atomic(
            platform.session_state_path(), bytes.data(), bytes.size())) {
        out_error = "failed to atomically write save file";
        return false;
    }
    return true;
}

bool load_persistent_game_state(
    const PlatformServices &platform,
    PersistentGameState &out,
    std::string &out_error) {
    std::vector<uint8_t> bytes;
    if (!platform.read_binary_file(platform.session_state_path(), bytes)) {
        out_error = "save file not found";
        return false;
    }
    return decode_persistent_game_state(bytes, out, out_error);
}
