#include "engine_world/block_picking.hpp"

#include <algorithm>
#include <cmath>

namespace {

bool solid_block(const BlockWorld &world, const BlockAddress &addr,
                 bool &resident) {
  const VoxelChunk *chunk = world.find_chunk(addr);
  resident = chunk != nullptr;
  if (chunk == nullptr) {
    return false;
  }
  return chunk->solid(addr.block.x, addr.block.y, addr.block.z) &&
         chunk->material(addr.block.x, addr.block.y, addr.block.z) !=
             VoxelMaterial::Air;
}

glm::dvec3 radial_up_at(const BlockWorld &world, const glm::dvec3 &position) {
  const glm::dvec3 radial = position - world.planet().center;
  return glm::dot(radial, radial) > 1.0e-9 ? glm::normalize(radial)
                                           : glm::dvec3(0.0, 1.0, 0.0);
}

} // namespace

BlockPick pick_block(const BlockWorld &world, const glm::vec3 &ray_origin,
                     const glm::vec3 &ray_dir, const glm::vec3 &reach_origin,
                     float reach, float max_distance) {
  // A tenth of a block keeps thin diagonal crossings from skipping a cell.
  const float step = static_cast<float>(world.config().block_size) * 0.1f;
  const double surface_band = world.max_surface_height_above_base() +
                              static_cast<double>(max_distance) + 32.0;
  BlockPick pick;
  std::optional<BlockAddress> last_empty;
  std::optional<BlockAddress> previous_sample;
  for (float d = step; d <= max_distance; d += step) {
    const glm::vec3 sample = ray_origin + ray_dir * d;
    const double radial_distance =
        glm::length(glm::dvec3(sample) - world.planet().center);
    if (std::abs(radial_distance - world.planet().radius) > surface_band) {
      continue;
    }
    const BlockAddress addr = world.address_from_world(glm::dvec3(sample));
    if (previous_sample.has_value() && *previous_sample == addr) {
      continue;
    }
    previous_sample = addr;
    bool resident = false;
    if (!solid_block(world, addr, resident)) {
      last_empty = resident ? std::optional<BlockAddress>(addr) : std::nullopt;
      continue;
    }
    if (glm::length(sample - reach_origin) > reach) {
      break;
    }
    pick.hit = addr;
    pick.place = last_empty;
    if (last_empty.has_value()) {
      const glm::dvec3 offset = world.world_from_address(*last_empty) -
                                world.world_from_address(addr);
      if (glm::dot(offset, offset) > 1.0e-9) {
        pick.face_normal = glm::vec3(glm::normalize(offset));
      }
    }
    break;
  }
  return pick;
}

bool block_overlaps_capsule(const BlockWorld &world, const BlockAddress &block,
                            const glm::dvec3 &feet, double radius,
                            double height) {
  const glm::dvec3 center = world.world_from_address(block);
  const glm::dvec3 up = radial_up_at(world, feet);
  const double clamped_height = std::max(height, 2.0 * radius);
  const glm::dvec3 segment_start = feet + up * radius;
  const glm::dvec3 segment = up * (clamped_height - 2.0 * radius);
  const double t = std::clamp(
      glm::dot(center - segment_start, segment) /
          std::max(glm::dot(segment, segment), 1.0e-9),
      0.0, 1.0);
  const glm::dvec3 closest = segment_start + segment * t;
  // The inscribed sphere (slightly shrunk) lets players build right beside
  // or beneath themselves while rejecting blocks inside the body.
  const double half_block = 0.5 * world.config().block_size;
  return glm::length(center - closest) < radius + half_block * 0.9;
}

std::vector<BlockAddress> chunks_touching_block(const BlockWorld &world,
                                                const BlockAddress &block) {
  BlockAddress base = block;
  base.block = glm::ivec3(0);
  std::vector<BlockAddress> chunks{base};
  const int32_t last = world.config().chunk_size - 1;
  const auto edge_offset = [last](int32_t coordinate) {
    return coordinate == 0 ? -1 : (coordinate == last ? 1 : 0);
  };
  const glm::ivec3 offset(edge_offset(block.block.x),
                          edge_offset(block.block.y),
                          edge_offset(block.block.z));
  for (int32_t dx = std::min(0, offset.x); dx <= std::max(0, offset.x); ++dx) {
    for (int32_t dy = std::min(0, offset.y); dy <= std::max(0, offset.y);
         ++dy) {
      for (int32_t dz = std::min(0, offset.z); dz <= std::max(0, offset.z);
           ++dz) {
        if (dx == 0 && dy == 0 && dz == 0) {
          continue;
        }
        BlockAddress neighbor{};
        if (world.offset_chunk_address(base, dx, dy, dz, neighbor) &&
            std::find(chunks.begin(), chunks.end(), neighbor) ==
                chunks.end()) {
          chunks.push_back(neighbor);
        }
      }
    }
  }
  return chunks;
}
