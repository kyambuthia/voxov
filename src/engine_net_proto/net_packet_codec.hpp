#pragma once

#include "engine_net_proto/net_types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

bool net_decode_header(const uint8_t *data, size_t size,
                       NetPacketHeader &out);

#define VOXOV_DECLARE_NET_CODEC(Type)                                          \
  bool net_encode_message(NetMsgType type, uint32_t sequence,                 \
                          const Type &payload, std::vector<uint8_t> &out,      \
                          uint8_t flags = 0);                                  \
  bool net_decode_message(const uint8_t *data, size_t size,                   \
                          NetMsgType expected_type, Type &out)

VOXOV_DECLARE_NET_CODEC(NetTickInput);
VOXOV_DECLARE_NET_CODEC(NetSnapshot);
VOXOV_DECLARE_NET_CODEC(NetChunkInterest);
VOXOV_DECLARE_NET_CODEC(NetChunkState);
VOXOV_DECLARE_NET_CODEC(NetAssignPlayer);
VOXOV_DECLARE_NET_CODEC(NetProtocolInfo);
VOXOV_DECLARE_NET_CODEC(NetSessionInfo);
VOXOV_DECLARE_NET_CODEC(NetPlayerState);
VOXOV_DECLARE_NET_CODEC(NetPlayerRemove);

#undef VOXOV_DECLARE_NET_CODEC
