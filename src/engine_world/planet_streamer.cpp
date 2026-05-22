#include "engine_world/planet_streamer.hpp"

#include "engine_world/planet_terrain.hpp"

#include <utility>

namespace {
constexpr uint64_t k_mesh_id_namespace = 0x5658504c00000000ull;
constexpr uint64_t k_fnv_offset_basis = 14695981039346656037ull;
constexpr uint64_t k_fnv_prime = 1099511628211ull;

uint64_t mix_u64(uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58476d1ce4e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d049bb133111ebull;
  value ^= value >> 31u;
  return value;
}

uint64_t chunk_key_bits(const PlanetChunkId &id) {
  uint64_t value = k_fnv_offset_basis;
  auto append = [&value](uint64_t component) {
    value ^= component;
    value *= k_fnv_prime;
  };

  append(static_cast<uint8_t>(id.face));
  append(static_cast<uint32_t>(id.x));
  append(static_cast<uint32_t>(id.y));
  append(static_cast<uint32_t>(id.lod));
  return value;
}

bool same_planet_definition(const PlanetDefinition &a,
                            const PlanetDefinition &b) {
  return a.center == b.center && a.radius == b.radius &&
         a.voxel_size == b.voxel_size &&
         a.chunks_per_face == b.chunks_per_face && a.seed == b.seed;
}
} // namespace

size_t PlanetChunkIdHash::operator()(const PlanetChunkId &id) const noexcept {
  return static_cast<size_t>(mix_u64(chunk_key_bits(id)));
}

void PlanetStreamer::init(const PlanetDefinition &planet, uint32_t max_lod) {
  planet_ = planet;
  quadtree_.init(planet_, max_lod);
  lod_selector_ = PlanetLODSelector{};
  resident_chunks_.clear();
  visible_meshes_.clear();
  frame_index_ = 0;
  initialized_ = true;
}

void PlanetStreamer::update(const glm::dvec3 &camera_pos,
                            const glm::mat4 &view_projection,
                            float screen_height_pixels) {
  if (!initialized_) {
    return;
  }

  ++frame_index_;
  for (auto &[id, chunk] : resident_chunks_) {
    (void)id;
    chunk.requested = false;
    chunk.visible = false;
  }

  LODSelectionResult selection = lod_selector_.select(
      quadtree_, camera_pos, view_projection, screen_height_pixels,
      config_.lod_error_threshold_pixels);

  for (const int32_t node_index : selection.new_nodes) {
    const PlanetQuadtreeNode *node = quadtree_.node(node_index);
    if (node == nullptr) {
      continue;
    }
    ensure_requested_chunk(*node, node_index);
  }

  generate_requested_chunks(selection.new_nodes);
  rebuild_visible_meshes(selection.visible_nodes);
  evict_chunks(selection.evict_nodes);
}

const std::vector<RenderMesh> &
PlanetStreamer::update(const PlanetRenderRequest &request) {
  if (!initialized_ || quadtree_.max_lod() != request.max_lod ||
      !same_planet_definition(planet_, request.planet)) {
    init(request.planet, request.max_lod);
  }

  update(request.camera_world_position, request.view_projection,
         request.screen_height_pixels);
  return visible_meshes_;
}

bool PlanetStreamer::is_chunk_resident(const PlanetChunkId &id) const {
  const auto it = resident_chunks_.find(id);
  return it != resident_chunks_.end() &&
         it->second.state == PlanetChunkResidencyState::Resident;
}

const RenderMesh *PlanetStreamer::resident_mesh(const PlanetChunkId &id) const {
  const auto it = resident_chunks_.find(id);
  if (it == resident_chunks_.end() ||
      it->second.state != PlanetChunkResidencyState::Resident) {
    return nullptr;
  }
  return &it->second.mesh;
}

std::optional<PlanetResidentChunk>
PlanetStreamer::resident_chunk(const PlanetChunkId &id) const {
  const auto it = resident_chunks_.find(id);
  if (it == resident_chunks_.end()) {
    return std::nullopt;
  }
  return it->second;
}

double PlanetStreamer::height_above_base_at_direction(
    const glm::dvec3 &direction) const {
  return planet_terrain_height_above_base_at_direction(planet_, direction);
}

double PlanetStreamer::max_height_above_base() const {
  return planet_terrain_max_height_above_base(planet_);
}

uint64_t PlanetStreamer::stable_mesh_id(const PlanetChunkId &id) {
  const uint64_t mixed = mix_u64(chunk_key_bits(id));
  const uint64_t mesh_id = k_mesh_id_namespace ^ (mixed & 0x0000ffffffffffffull);
  return mesh_id == 0 ? k_mesh_id_namespace : mesh_id;
}

PlanetResidentChunk &
PlanetStreamer::ensure_requested_chunk(const PlanetQuadtreeNode &node,
                                       int32_t node_index) {
  PlanetResidentChunk &chunk = resident_chunks_[node.id];
  if (chunk.state == PlanetChunkResidencyState::Empty) {
    chunk.id = node.id;
    chunk.mesh_id = stable_mesh_id(node.id);
    chunk.state = PlanetChunkResidencyState::Requested;
  }

  chunk.node_index = node_index;
  chunk.requested = true;
  chunk.last_requested_frame = frame_index_;
  return chunk;
}

void PlanetStreamer::generate_requested_chunks(
    const std::vector<int32_t> &requested_nodes) {
  uint32_t generated = 0;
  for (const int32_t node_index : requested_nodes) {
    if (generated >= config_.generation_budget_per_update) {
      break;
    }

    PlanetQuadtreeNode *node = quadtree_.node(node_index);
    if (node == nullptr) {
      continue;
    }

    PlanetResidentChunk &chunk = ensure_requested_chunk(*node, node_index);
    if (chunk.state == PlanetChunkResidencyState::Resident ||
        chunk.state == PlanetChunkResidencyState::Generating) {
      continue;
    }

    // Synchronous today; this state boundary is where a background job would be
    // queued and later completed into Resident.
    chunk.state = PlanetChunkResidencyState::Generating;
    node->state = QuadtreeNodeState::Generating;

    RenderMesh mesh = build_single_face_planet_terrain_mesh(planet_, node->id);
    mesh.mesh_id = chunk.mesh_id;

    chunk.mesh = std::move(mesh);
    chunk.state = PlanetChunkResidencyState::Resident;
    chunk.last_visible_frame = frame_index_;

    node->state = QuadtreeNodeState::Resident;
    node->mesh_handle = static_cast<int32_t>(chunk.mesh_id & 0x7fffffffu);
    ++generated;
  }
}

void PlanetStreamer::rebuild_visible_meshes(
    const std::vector<int32_t> &visible_nodes) {
  visible_meshes_.clear();
  const size_t max_visible = static_cast<size_t>(config_.max_visible_chunks);
  visible_meshes_.reserve(std::min(visible_nodes.size(), max_visible));

  for (const int32_t node_index : visible_nodes) {
    if (visible_meshes_.size() >= max_visible) {
      break;
    }

    PlanetQuadtreeNode *node = quadtree_.node(node_index);
    if (node == nullptr) {
      continue;
    }

    auto it = resident_chunks_.find(node->id);
    if (it == resident_chunks_.end() ||
        it->second.state != PlanetChunkResidencyState::Resident) {
      continue;
    }

    PlanetResidentChunk &chunk = it->second;
    chunk.visible = true;
    chunk.last_visible_frame = frame_index_;
    node->state = QuadtreeNodeState::Resident;
    node->last_used_frame = frame_index_;
    visible_meshes_.push_back(chunk.mesh);
  }
}

void PlanetStreamer::evict_chunks(const std::vector<int32_t> &evict_nodes) {
  for (const int32_t node_index : evict_nodes) {
    PlanetQuadtreeNode *node = quadtree_.node(node_index);
    if (node == nullptr) {
      continue;
    }

    auto it = resident_chunks_.find(node->id);
    if (it == resident_chunks_.end() || it->second.visible) {
      continue;
    }

    resident_chunks_.erase(it);
    node->state = QuadtreeNodeState::Empty;
    node->mesh_handle = -1;
  }
}
