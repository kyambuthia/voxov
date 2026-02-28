#include "engine_math/camera.hpp"
#include "engine_gameplay/player/player_controller.hpp"
#include "engine_net/net_common.hpp"
#include "engine_physics/avbd_solver.hpp"
#include "engine_physics/vehicle/vehicle_foundation.hpp"
#include "engine_physics/voxel/voxel_physics_bridge.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/voxel_chunk.hpp"
#include "platform/android_platform.hpp"
#include "platform/web_platform.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

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

void test_camera_yaw_response() {
    PlayerEntity player{};
    player.camera_rig.yaw = 0.0f;
    player.camera_rig.pitch = 0.0f;
    player.camera_rig.sensitivityMouse = 0.1f;

    InputState input{};
    input.look_delta.x = 10.0f;
    input.look_delta.y = 0.0f;

    const glm::vec3 forward_before = PlayerControllerSystem::orbit_forward_from_angles(player.camera_rig.yaw, player.camera_rig.pitch);
    PlayerControllerSystem::update_camera_rig(player, input, false, 1.0f / 60.0f);
    const glm::vec3 forward_after = PlayerControllerSystem::orbit_forward_from_angles(player.camera_rig.yaw, player.camera_rig.pitch);

    assert(player.camera_rig.yaw > 0.0f);
    assert(forward_after.x > forward_before.x);
}

void test_strafe_axis_sign() {
    const MovementDebug basis = PlayerControllerSystem::compute_movement_vectors(0.0f, glm::vec2(0.0f, 0.0f));
    assert(std::fabs(std::fabs(basis.right.x) - 1.0f) < 0.0001f);
    assert(std::fabs(basis.right.y) < 0.0001f);
    assert(std::fabs(basis.right.z) < 0.0001f);

    const float right_sign = (basis.right.x >= 0.0f) ? 1.0f : -1.0f;
    const MovementDebug move_d = PlayerControllerSystem::compute_movement_vectors(0.0f, glm::vec2(1.0f, 0.0f));
    const MovementDebug move_a = PlayerControllerSystem::compute_movement_vectors(0.0f, glm::vec2(-1.0f, 0.0f));
    assert(move_d.desired.x * right_sign > 0.0f);
    assert(move_a.desired.x * right_sign < 0.0f);

    const MovementDebug move_w = PlayerControllerSystem::compute_movement_vectors(0.0f, glm::vec2(0.0f, 1.0f));
    const MovementDebug move_s = PlayerControllerSystem::compute_movement_vectors(0.0f, glm::vec2(0.0f, -1.0f));
    assert(move_w.desired.z > 0.0f);
    assert(move_s.desired.z < 0.0f);
}

void test_player_settles_on_ground() {
    VoxelChunk chunk;
    chunk.generate_flat_ground(0);
    VoxelCollisionWorld collision_world(&chunk);

    PlayerEntity player = PlayerControllerSystem::spawn_player(collision_world);
    player.transform.position = glm::vec3(8.0f, 3.0f, 8.0f);
    player.controller.grounded = false;
    player.controller.velocity = glm::vec3(0.0f);

    InputState input{};
    for (int i = 0; i < 120; ++i) {
        PlayerControllerSystem::simulate_fixed(player, input, collision_world, 1.0f / 60.0f, false);
    }

    const float settled_y = player.transform.position.y;
    assert(player.controller.grounded);
    assert(settled_y > 0.8f);
    assert(settled_y < 1.3f);

    for (int i = 0; i < 60; ++i) {
        PlayerControllerSystem::simulate_fixed(player, input, collision_world, 1.0f / 60.0f, false);
    }
    assert(player.transform.position.y >= settled_y - 0.02f);
}

void test_player_animation_state_transitions() {
    VoxelChunk chunk;
    chunk.generate_flat_ground(0);
    VoxelCollisionWorld collision_world(&chunk);

    PlayerEntity player = PlayerControllerSystem::spawn_player(collision_world);
    player.transform.position = glm::vec3(8.0f, 1.05f, 8.0f);
    player.controller.grounded = true;
    player.controller.velocity = glm::vec3(0.0f);

    InputState input{};
    PlayerControllerSystem::simulate_fixed(player, input, collision_world, 1.0f / 60.0f, false);
    assert(player.anim_state == PlayerAnimState::Idle);

    input.move = glm::vec2(0.0f, 1.0f);
    input.sprint_held = false;
    input.crouch_held = false;
    PlayerControllerSystem::simulate_fixed(player, input, collision_world, 1.0f / 60.0f, false);
    assert(player.anim_state == PlayerAnimState::Walk);

    input.sprint_held = true;
    input.crouch_held = false;
    PlayerControllerSystem::simulate_fixed(player, input, collision_world, 1.0f / 60.0f, false);
    assert(player.anim_state == PlayerAnimState::Run);

    input.sprint_held = false;
    input.crouch_held = true;
    PlayerControllerSystem::simulate_fixed(player, input, collision_world, 1.0f / 60.0f, false);
    assert(player.anim_state == PlayerAnimState::Crawl);

    input.move = glm::vec2(0.0f);
    input.crouch_held = false;
    input.jump_pressed = true;
    player.controller.grounded = true;
    PlayerControllerSystem::simulate_fixed(player, input, collision_world, 1.0f / 60.0f, false);
    assert(player.anim_state == PlayerAnimState::Jump);
}

void test_avbd_solver_lifecycle() {
    AvbdSolver solver;
    EnginePhysicsSettings settings{};
    settings.gravity = -9.81f;
    settings.solver_backend = PhysicsSolverBackend::AvbdExperimental;
    solver.init(settings);
    solver.create_minimal_test_scene();
    assert(!solver.vertices().empty());
    assert(!solver.bodies().empty());

    for (int i = 0; i < 180; ++i) {
        solver.step(1.0f / 60.0f);
    }

    bool found_ground_contact = false;
    for (const AvbdVertexState &v : solver.vertices()) {
        assert(std::isfinite(v.position.x));
        assert(std::isfinite(v.position.y));
        assert(std::isfinite(v.position.z));
        assert(std::isfinite(v.velocity.x));
        assert(std::isfinite(v.velocity.y));
        assert(std::isfinite(v.velocity.z));
        if (v.position.y < 0.05f) {
            found_ground_contact = true;
        }
    }
    assert(found_ground_contact);

    solver.shutdown();
    solver.step(1.0f / 60.0f);
}

void test_web_platform_state() {
    WebPlatform platform;
    assert(platform.init(640, 360));
    assert(platform.active());
    assert(platform.width() == 640);
    assert(platform.height() == 360);
    assert(platform.poll_events());
    assert(platform.tick() == 1);
    platform.set_focused(false);
    assert(!platform.active());
    assert(!platform.poll_events());
    platform.set_focused(true);
    assert(platform.poll_events());
    platform.shutdown();
    assert(!platform.active());
}

void test_android_platform_state() {
    AndroidPlatform platform;
    assert(platform.init(1920, 1080));
    assert(platform.active());
    assert(platform.width() == 1920);
    assert(platform.height() == 1080);
    assert(platform.poll_events());
    platform.on_pause();
    assert(!platform.active());
    assert(!platform.poll_events());
    platform.on_resume();
    platform.on_resize(1280, 720);
    assert(platform.width() == 1280);
    assert(platform.height() == 720);
    assert(platform.active());
    assert(platform.frame_time_seconds() > 0.0);
    platform.shutdown();
    assert(!platform.active());
}

void test_vehicle_foundation_fixed_step_counter() {
    FixedStepCounter counter(1.0 / 60.0);
    const uint32_t s0 = counter.consume(1.0 / 30.0);
    assert(s0 == 2);
    const uint32_t s1 = counter.consume(1.0 / 120.0);
    assert(s1 == 0 || s1 == 1);
    assert(counter.tick() >= 2);
    assert(counter.alpha() >= 0.0 && counter.alpha() <= 1.0);
}

void test_vehicle_kinematic_determinism() {
    VehicleKinematicState a{};
    VehicleKinematicState b{};
    const VehicleControlInput input{1.0f, 0.0f, 0.4f, 0.0f};
    const VehiclePhysicsTuning tuning{};
    for (int i = 0; i < 240; ++i) {
        integrate_vehicle_kinematics(a, input, tuning, 1.0f / 60.0f);
        integrate_vehicle_kinematics(b, input, tuning, 1.0f / 60.0f);
    }
    assert(glm::length(a.position - b.position) < 1.0e-4f);
    assert(std::isfinite(a.position.x) && std::isfinite(a.position.y) && std::isfinite(a.position.z));
}

void test_aircraft_kinematic_determinism() {
    AircraftKinematicState a{};
    AircraftKinematicState b{};
    const AircraftControlInput input{0.8f, 0.2f, -0.1f, 0.05f};
    const AircraftPhysicsTuning tuning{};
    for (int i = 0; i < 300; ++i) {
        integrate_aircraft_kinematics(a, input, tuning, 1.0f / 120.0f);
        integrate_aircraft_kinematics(b, input, tuning, 1.0f / 120.0f);
    }
    assert(glm::length(a.position - b.position) < 1.0e-4f);
    assert(std::isfinite(a.euler.x) && std::isfinite(a.euler.y) && std::isfinite(a.euler.z));
}

void test_voxel_physics_bridge_chunk_tracking() {
    VoxelPhysicsBridge bridge;
    VoxelChunk chunk;
    chunk.generate_flat_ground(2);

    const VoxelChunkCoord coord{1, -2};
    assert(!bridge.has_chunk(coord));
    bridge.register_chunk(coord, chunk);
    assert(bridge.has_chunk(coord));
    assert(bridge.chunk_count() == 1);

    assert(bridge.take_chunk_dirty(coord));
    assert(!bridge.take_chunk_dirty(coord));

    bridge.unregister_chunk(coord);
    assert(!bridge.has_chunk(coord));
    assert(bridge.chunk_count() == 0);
}

void test_voxel_physics_bridge_shape_build() {
    VoxelPhysicsBridge bridge;
    VoxelChunk chunk;
    chunk.generate_flat_ground(0);
    bridge.register_chunk(VoxelChunkCoord{2, 3}, chunk);

    const std::vector<VoxelStaticShape> shapes = bridge.build_chunk_shapes(VoxelChunkCoord{2, 3});
    assert(!shapes.empty());

    bool found_chunk_offset = false;
    for (const VoxelStaticShape &shape : shapes) {
        assert(shape.max.x > shape.min.x);
        assert(shape.max.y > shape.min.y);
        assert(shape.max.z > shape.min.z);
        if (shape.min.x >= static_cast<float>(2 * VoxelChunk::CHUNK_X) &&
            shape.min.z >= static_cast<float>(3 * VoxelChunk::CHUNK_Z)) {
            found_chunk_offset = true;
        }
    }
    assert(found_chunk_offset);
}

}

int main() {
    test_camera_vectors();
    test_net_pod_serialization();
    test_chunk_meshing();
    test_camera_yaw_response();
    test_strafe_axis_sign();
    test_player_settles_on_ground();
    test_player_animation_state_transitions();
    test_avbd_solver_lifecycle();
    test_web_platform_state();
    test_android_platform_state();
    test_vehicle_foundation_fixed_step_counter();
    test_vehicle_kinematic_determinism();
    test_aircraft_kinematic_determinism();
    test_voxel_physics_bridge_chunk_tracking();
    test_voxel_physics_bridge_shape_build();
    return 0;
}
