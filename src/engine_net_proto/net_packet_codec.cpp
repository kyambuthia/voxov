#include "engine_net_proto/net_packet_codec.hpp"

#include <bit>
#include <cstring>
#include <type_traits>
#include <utility>

namespace {
class Writer {
public:
  template <typename T> void unsigned_value(T value) {
    static_assert(std::is_unsigned_v<T>);
    uint64_t remaining = static_cast<uint64_t>(value);
    for (size_t i = 0; i < sizeof(T); ++i) {
      bytes.push_back(static_cast<uint8_t>(remaining & 0xffu));
      remaining >>= 8u;
    }
  }

  void i16(int16_t value) { unsigned_value(std::bit_cast<uint16_t>(value)); }
  void f32(float value) { unsigned_value(std::bit_cast<uint32_t>(value)); }
  void f64(double value) { unsigned_value(std::bit_cast<uint64_t>(value)); }
  void raw(const void *data, size_t size) {
    if (size == 0) return;
    const auto *first = static_cast<const uint8_t *>(data);
    bytes.insert(bytes.end(), first, first + size);
  }

  std::vector<uint8_t> bytes;
};

bool payload_type_matches(NetMsgType type, const NetTickInput &) {
  return type == NetMsgType::Input;
}
bool payload_type_matches(NetMsgType type, const NetSnapshot &) {
  return type == NetMsgType::Snapshot;
}
bool payload_type_matches(NetMsgType type, const NetChunkInterest &) {
  return type == NetMsgType::ChunkInterest;
}
bool payload_type_matches(NetMsgType type, const NetChunkState &) {
  return type == NetMsgType::ChunkState;
}
bool payload_type_matches(NetMsgType type, const NetAssignPlayer &) {
  return type == NetMsgType::AssignPlayer;
}
bool payload_type_matches(NetMsgType type, const NetProtocolInfo &) {
  return type == NetMsgType::ProtocolInfo;
}
bool payload_type_matches(NetMsgType type, const NetSessionInfo &) {
  return type == NetMsgType::SessionInfo;
}
bool payload_type_matches(NetMsgType type, const NetPlayerState &) {
  return type == NetMsgType::PlayerState;
}
bool payload_type_matches(NetMsgType type, const NetPlayerRemove &) {
  return type == NetMsgType::PlayerRemove;
}

class Reader {
public:
  Reader(const uint8_t *data, size_t size) : data_(data), size_(size) {}

  template <typename T> bool unsigned_value(T &out) {
    static_assert(std::is_unsigned_v<T>);
    if (offset_ > size_ || size_ - offset_ < sizeof(T)) {
      return false;
    }
    T value = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
      value |= static_cast<T>(data_[offset_ + i]) << (i * 8u);
    }
    offset_ += sizeof(T);
    out = value;
    return true;
  }

  bool i16(int16_t &out) {
    uint16_t value = 0;
    if (!unsigned_value(value)) return false;
    out = std::bit_cast<int16_t>(value);
    return true;
  }
  bool f32(float &out) {
    uint32_t value = 0;
    if (!unsigned_value(value)) return false;
    out = std::bit_cast<float>(value);
    return true;
  }
  bool f64(double &out) {
    uint64_t value = 0;
    if (!unsigned_value(value)) return false;
    out = std::bit_cast<double>(value);
    return true;
  }
  bool raw(void *out, size_t size) {
    if (offset_ > size_ || size_ - offset_ < size) return false;
    std::memcpy(out, data_ + offset_, size);
    offset_ += size;
    return true;
  }
  bool complete() const { return offset_ == size_; }

private:
  const uint8_t *data_ = nullptr;
  size_t size_ = 0;
  size_t offset_ = 0;
};

void write_world(Writer &writer, const NetWorldAddress &world) {
  writer.unsigned_value(world.system_id);
  writer.unsigned_value(world.body_id);
  writer.unsigned_value(world.frame);
  writer.raw(world.reserved, sizeof(world.reserved));
}

bool read_world(Reader &reader, NetWorldAddress &world) {
  return reader.unsigned_value(world.system_id) &&
      reader.unsigned_value(world.body_id) &&
      reader.unsigned_value(world.frame) &&
      reader.raw(world.reserved, sizeof(world.reserved));
}

void write_chunk_coord(Writer &writer, const NetChunkCoord &coord) {
  writer.unsigned_value(coord.body_id);
  writer.unsigned_value(coord.face);
  writer.unsigned_value(coord.shell);
  writer.i16(coord.x);
  writer.i16(coord.y);
  writer.i16(coord.z);
}

bool read_chunk_coord(Reader &reader, NetChunkCoord &coord) {
  return reader.unsigned_value(coord.body_id) &&
      reader.unsigned_value(coord.face) &&
      reader.unsigned_value(coord.shell) && reader.i16(coord.x) &&
      reader.i16(coord.y) && reader.i16(coord.z);
}

void write_payload(Writer &writer, const NetTickInput &value) {
  writer.unsigned_value(value.tick);
  writer.unsigned_value(value.body_id);
  writer.f32(value.move_x);
  writer.f32(value.move_y);
  writer.f32(value.camera_yaw_deg);
  writer.unsigned_value(value.action_flags);
}

bool read_payload(Reader &reader, NetTickInput &value) {
  return reader.unsigned_value(value.tick) &&
      reader.unsigned_value(value.body_id) && reader.f32(value.move_x) &&
      reader.f32(value.move_y) && reader.f32(value.camera_yaw_deg) &&
      reader.unsigned_value(value.action_flags);
}

void write_payload(Writer &writer, const NetSnapshot &value) {
  writer.unsigned_value(value.player_id);
  writer.unsigned_value(value.tick);
  writer.unsigned_value(value.sequence);
  write_world(writer, value.world);
  writer.f64(value.x);
  writer.f64(value.y);
  writer.f64(value.z);
  writer.f32(value.vx);
  writer.f32(value.vy);
  writer.f32(value.vz);
}

bool read_payload(Reader &reader, NetSnapshot &value) {
  return reader.unsigned_value(value.player_id) &&
      reader.unsigned_value(value.tick) &&
      reader.unsigned_value(value.sequence) && read_world(reader, value.world) &&
      reader.f64(value.x) && reader.f64(value.y) && reader.f64(value.z) &&
      reader.f32(value.vx) && reader.f32(value.vy) && reader.f32(value.vz);
}

void write_payload(Writer &writer, const NetChunkInterest &value) {
  writer.unsigned_value(value.body_id);
  writer.unsigned_value(value.face);
  writer.unsigned_value(value.shell);
  writer.i16(value.center_x);
  writer.i16(value.center_y);
  writer.i16(value.center_z);
  writer.unsigned_value(value.radius);
}

bool read_payload(Reader &reader, NetChunkInterest &value) {
  return reader.unsigned_value(value.body_id) &&
      reader.unsigned_value(value.face) &&
      reader.unsigned_value(value.shell) && reader.i16(value.center_x) &&
      reader.i16(value.center_y) && reader.i16(value.center_z) &&
      reader.unsigned_value(value.radius);
}

void write_payload(Writer &writer, const NetChunkState &value) {
  write_chunk_coord(writer, value.coord);
  writer.unsigned_value(value.version);
  writer.unsigned_value(value.world_seed);
  writer.unsigned_value(value.content_type);
  writer.raw(value.reserved, sizeof(value.reserved));
}

bool read_payload(Reader &reader, NetChunkState &value) {
  return read_chunk_coord(reader, value.coord) &&
      reader.unsigned_value(value.version) &&
      reader.unsigned_value(value.world_seed) &&
      reader.unsigned_value(value.content_type) &&
      reader.raw(value.reserved, sizeof(value.reserved));
}

void write_payload(Writer &writer, const NetAssignPlayer &value) {
  writer.unsigned_value(value.player_id);
}
bool read_payload(Reader &reader, NetAssignPlayer &value) {
  return reader.unsigned_value(value.player_id);
}

void write_payload(Writer &writer, const NetProtocolInfo &value) {
  writer.unsigned_value(value.protocol_version);
  writer.unsigned_value(value.feature_flags);
  writer.unsigned_value(value.server_tick_hz);
  writer.unsigned_value(value.reserved);
}
bool read_payload(Reader &reader, NetProtocolInfo &value) {
  return reader.unsigned_value(value.protocol_version) &&
      reader.unsigned_value(value.feature_flags) &&
      reader.unsigned_value(value.server_tick_hz) &&
      reader.unsigned_value(value.reserved);
}

void write_payload(Writer &writer, const NetSessionInfo &value) {
  writer.raw(value.server_name, sizeof(value.server_name));
  writer.unsigned_value(value.world_seed);
  writer.unsigned_value(value.current_players);
  writer.unsigned_value(value.max_players);
  writer.unsigned_value(value.flags);
  writer.raw(value.reserved, sizeof(value.reserved));
}
bool read_payload(Reader &reader, NetSessionInfo &value) {
  return reader.raw(value.server_name, sizeof(value.server_name)) &&
      reader.unsigned_value(value.world_seed) &&
      reader.unsigned_value(value.current_players) &&
      reader.unsigned_value(value.max_players) &&
      reader.unsigned_value(value.flags) &&
      reader.raw(value.reserved, sizeof(value.reserved));
}

void write_payload(Writer &writer, const NetPlayerState &value) {
  writer.unsigned_value(value.player_id);
  writer.unsigned_value(value.tick);
  writer.unsigned_value(value.sequence);
  write_world(writer, value.world);
  writer.f64(value.x);
  writer.f64(value.y);
  writer.f64(value.z);
  writer.f32(value.vx);
  writer.f32(value.vy);
  writer.f32(value.vz);
  writer.unsigned_value(value.anim_state);
  writer.f32(value.anim_phase);
  writer.f32(value.anim_blend);
  writer.unsigned_value(value.character);
}
bool read_payload(Reader &reader, NetPlayerState &value) {
  return reader.unsigned_value(value.player_id) &&
      reader.unsigned_value(value.tick) &&
      reader.unsigned_value(value.sequence) && read_world(reader, value.world) &&
      reader.f64(value.x) && reader.f64(value.y) && reader.f64(value.z) &&
      reader.f32(value.vx) && reader.f32(value.vy) && reader.f32(value.vz) &&
      reader.unsigned_value(value.anim_state) && reader.f32(value.anim_phase) &&
      reader.f32(value.anim_blend) && reader.unsigned_value(value.character);
}

void write_payload(Writer &writer, const NetPlayerRemove &value) {
  writer.unsigned_value(value.player_id);
}
bool read_payload(Reader &reader, NetPlayerRemove &value) {
  return reader.unsigned_value(value.player_id);
}

template <typename T>
bool encode(NetMsgType type, uint32_t sequence, const T &payload,
            std::vector<uint8_t> &out, uint8_t flags) {
  if (!payload_type_matches(type, payload)) return false;
  Writer payload_writer;
  write_payload(payload_writer, payload);
  if (payload_writer.bytes.size() > k_net_max_payload_bytes) return false;

  Writer packet;
  packet.unsigned_value(k_net_packet_magic);
  packet.unsigned_value(k_net_protocol_version);
  packet.unsigned_value(static_cast<uint8_t>(type));
  packet.unsigned_value(flags);
  packet.unsigned_value(sequence);
  packet.unsigned_value(static_cast<uint16_t>(payload_writer.bytes.size()));
  packet.raw(payload_writer.bytes.data(), payload_writer.bytes.size());
  out = std::move(packet.bytes);
  return true;
}

template <typename T>
bool decode(const uint8_t *data, size_t size, NetMsgType expected_type, T &out) {
  NetPacketHeader header{};
  if (!net_decode_header(data, size, header) ||
      header.type != static_cast<uint8_t>(expected_type)) {
    return false;
  }
  Reader payload(data + k_net_wire_header_size, header.payload_size);
  T decoded{};
  if (!read_payload(payload, decoded) || !payload.complete()) return false;
  out = decoded;
  return true;
}
} // namespace

bool net_decode_header(const uint8_t *data, size_t size,
                       NetPacketHeader &out) {
  if (data == nullptr || size < k_net_wire_header_size) return false;
  Reader reader(data, k_net_wire_header_size);
  NetPacketHeader header{};
  if (!reader.unsigned_value(header.magic) ||
      !reader.unsigned_value(header.version) ||
      !reader.unsigned_value(header.type) ||
      !reader.unsigned_value(header.flags) ||
      !reader.unsigned_value(header.sequence) ||
      !reader.unsigned_value(header.payload_size) || !reader.complete() ||
      !net_header_basic_valid(header) ||
      size != k_net_wire_header_size + header.payload_size) {
    return false;
  }
  out = header;
  return true;
}

#define VOXOV_DEFINE_NET_CODEC(Type)                                           \
  bool net_encode_message(NetMsgType type, uint32_t sequence,                 \
                          const Type &payload, std::vector<uint8_t> &out,      \
                          uint8_t flags) {                                     \
    return encode(type, sequence, payload, out, flags);                       \
  }                                                                            \
  bool net_decode_message(const uint8_t *data, size_t size,                   \
                          NetMsgType expected_type, Type &out) {               \
    return decode(data, size, expected_type, out);                            \
  }

VOXOV_DEFINE_NET_CODEC(NetTickInput)
VOXOV_DEFINE_NET_CODEC(NetSnapshot)
VOXOV_DEFINE_NET_CODEC(NetChunkInterest)
VOXOV_DEFINE_NET_CODEC(NetChunkState)
VOXOV_DEFINE_NET_CODEC(NetAssignPlayer)
VOXOV_DEFINE_NET_CODEC(NetProtocolInfo)
VOXOV_DEFINE_NET_CODEC(NetSessionInfo)
VOXOV_DEFINE_NET_CODEC(NetPlayerState)
VOXOV_DEFINE_NET_CODEC(NetPlayerRemove)

#undef VOXOV_DEFINE_NET_CODEC
