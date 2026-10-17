#include "engine_gameplay/vehicles/voxel_model.hpp"
#include "platform/platform_services.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <unordered_map>

namespace {

// .vox is Z-up and we are Y-up, so a stored (x, y, z) is model (x, z, y).
struct Key {
  int x = 0, y = 0, z = 0;
  bool operator==(const Key &other) const {
    return x == other.x && y == other.y && z == other.z;
  }
};

struct KeyHash {
  std::size_t operator()(const Key &k) const {
    // Coordinates are bounded by 256, so pack rather than combine hashes.
    return std::size_t(k.x + 512) | (std::size_t(k.y + 512) << 10) |
           (std::size_t(k.z + 512) << 20);
  }
};

// The six faces of a unit cube as (normal, four corner offsets),
// counter-clockwise seen from outside to match the opaque pipeline.
struct FaceSpec {
  glm::ivec3 normal;
  glm::vec3 corners[4];
};

constexpr std::array<FaceSpec, 6> kFaces{{
    {{1, 0, 0}, {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}},
    {{-1, 0, 0}, {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}},
    {{0, 1, 0}, {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}},
    {{0, -1, 0}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},
    {{0, 0, 1}, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}},
    {{0, 0, -1}, {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}},
}};

uint32_t read_u32(const std::vector<uint8_t> &data, size_t offset) {
  return uint32_t(data[offset]) | (uint32_t(data[offset + 1]) << 8) |
         (uint32_t(data[offset + 2]) << 16) |
         (uint32_t(data[offset + 3]) << 24);
}

// sRGB bytes to the linear floats the scene shader expects.
float to_linear(uint8_t channel) {
  const float c = static_cast<float>(channel) / 255.0f;
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

} // namespace

VoxelModel parse_vox_model(const std::vector<uint8_t> &data,
                           float voxel_size_meters) {
  VoxelModel model;
  if (data.size() < 20 || std::memcmp(data.data(), "VOX ", 4) != 0 ||
      std::memcmp(data.data() + 8, "MAIN", 4) != 0 ||
      read_u32(data, 12) != 0 || read_u32(data, 16) != data.size() - 20 ||
      !std::isfinite(voxel_size_meters) || voxel_size_meters <= 0.0f) {
    return model;
  }

  std::unordered_map<Key, uint8_t, KeyHash> voxels;
  std::map<uint8_t, glm::vec3> palette;

  size_t pos = 8;
  while (pos + 12 <= data.size()) {
    char tag[5] = {};
    std::memcpy(tag, data.data() + pos, 4);
    const uint32_t content = read_u32(data, pos + 4);
    const uint32_t children = read_u32(data, pos + 8);
    const size_t body = pos + 12;
    if (content > data.size() - body ||
        children > data.size() - body - content) {
      return model;
    }

    if (std::strcmp(tag, "XYZI") == 0) {
      if (content < 4) return model;
      const uint32_t count = read_u32(data, body);
      if (count > (content - 4) / 4 || content - 4 != size_t(count) * 4) {
        return model;
      }
      for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *v = data.data() + body + 4 + size_t(i) * 4;
        voxels[Key{v[0], v[2], v[1]}] = v[3];
      }
    } else if (std::strcmp(tag, "RGBA") == 0) {
      if (content != 1024) return model;
      for (int i = 0; i < 255; ++i) {
        const uint8_t *c = data.data() + body + size_t(i) * 4;
        if (c[3] != 0) {
          palette[static_cast<uint8_t>(i + 1)] =
              glm::vec3(to_linear(c[0]), to_linear(c[1]), to_linear(c[2]));
        }
      }
    }

    // MAIN carries its payload as children, so descend rather than skip.
    pos = std::strcmp(tag, "MAIN") == 0 ? body + content
                                        : body + content + children;
  }

  if (pos != data.size() || voxels.empty()) {
    return model;
  }

  glm::ivec3 lo(INT32_MAX), hi(INT32_MIN);
  for (const auto &[key, _] : voxels) {
    lo = glm::min(lo, glm::ivec3(key.x, key.y, key.z));
    hi = glm::max(hi, glm::ivec3(key.x, key.y, key.z));
  }
  const glm::vec3 centre = 0.5f * glm::vec3(lo + hi + glm::ivec3(1));

  model.quads.reserve(voxels.size());
  for (const auto &[key, index] : voxels) {
    const auto colour = palette.find(index);
    const glm::vec3 rgb =
        colour != palette.end() ? colour->second : glm::vec3(0.8f);
    for (const FaceSpec &face : kFaces) {
      const Key neighbour{key.x + face.normal.x, key.y + face.normal.y,
                          key.z + face.normal.z};
      if (voxels.count(neighbour) != 0) {
        continue; // hidden: the neighbour fills this face
      }
      VoxelQuad quad;
      quad.normal = glm::vec3(face.normal);
      quad.color = rgb;
      quad.material = index;
      for (int i = 0; i < 4; ++i) {
        quad.corners[i] =
            (glm::vec3(key.x, key.y, key.z) + face.corners[i] - centre) *
            voxel_size_meters;
      }
      model.quads.push_back(quad);
    }
  }

  model.size_meters = glm::vec3(hi - lo + glm::ivec3(1)) * voxel_size_meters;
  model.valid = true;
  return model;
}

const VoxelModel &load_voxel_model(const PlatformServices &services,
                                   const std::string &relative_path,
                                   float voxel_size_meters) {
  if (!std::isfinite(voxel_size_meters) || voxel_size_meters <= 0.0f) {
    static const VoxelModel invalid;
    return invalid;
  }
  static std::map<std::pair<std::string, float>, VoxelModel> cache;
  const auto key = std::make_pair(relative_path, voxel_size_meters);
  const auto cached = cache.find(key);
  if (cached != cache.end()) {
    return cached->second;
  }

  std::vector<uint8_t> data;
  for (const auto &candidate : services.candidate_asset_paths(relative_path)) {
    std::ifstream file(candidate, std::ios::binary);
    if (file) {
      data.assign(std::istreambuf_iterator<char>(file),
                  std::istreambuf_iterator<char>());
      break;
    }
  }

  VoxelModel model =
      data.empty() ? VoxelModel{} : parse_vox_model(data, voxel_size_meters);
  return cache.emplace(key, std::move(model)).first->second;
}

const VoxelModel &load_voxel_model(const std::string &relative_path,
                                   float voxel_size_meters) {
  return load_voxel_model(PlatformServices::desktop_default(), relative_path,
                          voxel_size_meters);
}
