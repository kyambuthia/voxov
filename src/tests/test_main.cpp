#include "engine_math/camera.hpp"
#include "engine_net/net_common.hpp"
#include "engine_world/voxel_chunk.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>

namespace {

void test_camera_vectors() {
    Camera camera;
    camera.transform.euler_radians = glm::vec3(0.0f, 0.0f, 0.0f);

    glm::vec3 f = camera.forward();
    glm::vec3 r = camera.right();

    assert(std::fabs(f.x) < 0.0001f);
    assert(std::fabs(f.y) < 0.0001f);
    assert(std::fabs(f.z + 1.0f) < 0.0001f);

    assert(std::fabs(r.x - 1.0f) < 0.0001f);
    assert(std::fabs(r.y) < 0.0001f);
    assert(std::fabs(r.z) < 0.0001f);
}

void test_net_pod_serialization() {
    NetSnapshot in{};
    in.tick = 42;
    in.x = 1.5f;
    in.y = -2.0f;
    in.z = 9.25f;

    uint8_t buffer[sizeof(NetSnapshot)]{};
    bool write_ok = net_write_pod(buffer, sizeof(buffer), in);
    assert(write_ok);

    NetSnapshot out{};
    bool read_ok = net_read_pod(buffer, sizeof(buffer), out);
    assert(read_ok);

    assert(out.tick == in.tick);
    assert(std::fabs(out.x - in.x) < 0.0001f);
    assert(std::fabs(out.y - in.y) < 0.0001f);
    assert(std::fabs(out.z - in.z) < 0.0001f);
}

void test_chunk_meshing() {
    VoxelChunk chunk;
    chunk.generate_heightmap_terrain();
    RenderMesh mesh = chunk.build_naive_mesh();

    assert(!mesh.vertices.empty());
    assert(!mesh.indices.empty());
    assert(mesh.indices.size() % 3 == 0);
}

}

int main() {
    test_camera_vectors();
    test_net_pod_serialization();
    test_chunk_meshing();
    return 0;
}
