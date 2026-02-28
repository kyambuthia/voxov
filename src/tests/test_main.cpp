#include "engine_math/camera.hpp"
#include "engine_gameplay/player/player_controller.hpp"
#include "engine_net/net_common.hpp"
#include "engine_physics/avbd_solver.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/voxel_chunk.hpp"
#include "platform/android_platform.hpp"
#include "platform/web_platform.hpp"

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
    solver.init(settings);
    for (int i = 0; i < 32; ++i) {
        solver.step(1.0f / 60.0f);
    }
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
    return 0;
}
