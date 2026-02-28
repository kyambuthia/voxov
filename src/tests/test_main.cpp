#include "engine_math/camera.hpp"
#include "engine_gameplay/minigames/minigames.hpp"
#include "engine_gameplay/player/player_controller.hpp"
#include "engine_net/net_common.hpp"
#include "engine_physics/avbd_solver.hpp"
#include "engine_physics/vehicle/aircraft_controller.hpp"
#include "engine_physics/vehicle/vehicle_damage_model.hpp"
#include "engine_physics/vehicle/ground_vehicle_controller.hpp"
#include "engine_physics/vehicle/vehicle_drivetrain.hpp"
#include "engine_physics/vehicle/vehicle_foundation.hpp"
#include "engine_physics/vehicle/vehicle_sandbox_scene.hpp"
#include "engine_physics/vehicle/voxel_vehicle_builder.hpp"
#include "engine_physics/voxel/voxel_physics_bridge.hpp"
#include "engine_world/physics/voxel_collision.hpp"
#include "engine_world/voxel_chunk.hpp"
#include "platform/android_platform.hpp"
#include "platform/web_platform.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>
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

void test_chunk_seed_determinism() {
    VoxelChunk a;
    VoxelChunk b;
    VoxelChunk c;

    a.generate_heightmap_terrain_seeded(12345u, 4, -2);
    b.generate_heightmap_terrain_seeded(12345u, 4, -2);
    c.generate_heightmap_terrain_seeded(12345u, 5, -2);

    const RenderMesh mesh_a = a.build_naive_mesh();
    const RenderMesh mesh_b = b.build_naive_mesh();
    const RenderMesh mesh_c = c.build_naive_mesh();

    assert(mesh_a.vertices.size() == mesh_b.vertices.size());
    assert(mesh_a.indices.size() == mesh_b.indices.size());
    for (size_t i = 0; i < mesh_a.vertices.size(); ++i) {
        const glm::vec3 pa = mesh_a.vertices[i].position;
        const glm::vec3 pb = mesh_b.vertices[i].position;
        assert(std::fabs(pa.x - pb.x) < 0.0001f);
        assert(std::fabs(pa.y - pb.y) < 0.0001f);
        assert(std::fabs(pa.z - pb.z) < 0.0001f);
    }

    bool different = mesh_a.vertices.size() != mesh_c.vertices.size() ||
                     mesh_a.indices.size() != mesh_c.indices.size();
    if (!different && !mesh_a.vertices.empty() && !mesh_c.vertices.empty()) {
        const glm::vec3 pa = mesh_a.vertices.front().position;
        const glm::vec3 pc = mesh_c.vertices.front().position;
        different = std::fabs(pa.x - pc.x) > 0.0001f ||
                    std::fabs(pa.y - pc.y) > 0.0001f ||
                    std::fabs(pa.z - pc.z) > 0.0001f;
    }
    assert(different);
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

void test_minigame_snake_runs() {
    MiniGameState state{};
    minigame_begin(state, MiniGameType::Snake, 11u);
    assert(state.active);
    assert(!state.completed);

    InputState input{};
    input.move = glm::vec2(1.0f, 0.0f);
    for (int i = 0; i < 80 && !state.completed; ++i) {
        minigame_tick(state, input, 1.0f / 30.0f);
    }

    assert(state.snake.length >= 3);
}

void test_minigame_golf_shot() {
    MiniGameState state{};
    minigame_begin(state, MiniGameType::Golf, 3u);
    assert(state.active);
    assert(state.golf.strokes == 0);

    InputState input{};
    input.interact_pressed = true;
    minigame_tick(state, input, 1.0f / 60.0f);
    assert(state.golf.strokes == 1);

    input.interact_pressed = false;
    for (int i = 0; i < 120; ++i) {
        minigame_tick(state, input, 1.0f / 60.0f);
    }
    assert(std::isfinite(state.golf.ball.x));
    assert(std::isfinite(state.golf.ball.y));
}

void test_minigame_tetris_progress() {
    MiniGameState state{};
    minigame_begin(state, MiniGameType::Tetris, 17u);
    assert(state.active);
    assert(!state.completed);

    InputState input{};
    for (int i = 0; i < 240 && !state.completed; ++i) {
        input.move.x = (i % 40 < 20) ? -1.0f : 1.0f;
        input.jump_pressed = (i % 23) == 0;
        input.crouch_held = (i % 29) < 4;
        minigame_tick(state, input, 1.0f / 30.0f);
        input.jump_pressed = false;
    }

    assert(state.score >= 0);
}

void test_minigame_racing_completion() {
    MiniGameState state{};
    minigame_begin(state, MiniGameType::Racing, 5u);
    assert(state.active);
    assert(!state.completed);

    InputState input{};
    input.move = glm::vec2(0.0f, 1.0f);
    for (int i = 0; i < 7200 && !state.completed; ++i) {
        minigame_tick(state, input, 1.0f / 60.0f);
    }

    assert(state.racing.lap >= 1);
}

void test_minigame_tictactoe_places_marks() {
    MiniGameState state{};
    minigame_begin(state, MiniGameType::TicTacToe, 9u);
    assert(state.active);

    InputState input{};
    for (int i = 0; i < 6 && !state.completed; ++i) {
        input.interact_pressed = true;
        minigame_tick(state, input, 1.0f / 30.0f);
        input.interact_pressed = false;
        input.move.x = ((i % 2) == 0) ? 1.0f : -1.0f;
        minigame_tick(state, input, 1.0f / 30.0f);
    }

    int occupied = 0;
    for (uint8_t cell : state.tictactoe.board) {
        if (cell != 0) {
            occupied++;
        }
    }
    assert(occupied >= 2);
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

void test_voxel_vehicle_builder_mass_properties() {
    std::vector<VoxelMassCell> cells;
    for (int x = 0; x < 4; ++x) {
        for (int y = 0; y < 2; ++y) {
            for (int z = 0; z < 2; ++z) {
                cells.push_back({glm::ivec3(x, y, z), VoxelMassMaterial{650.0f}});
            }
        }
    }

    const VoxelVehicleBuildResult result = build_voxel_vehicle_properties(cells, 0.5f);
    assert(result.mass.total_mass_kg > 0.0f);
    assert(result.mass.center_of_mass.x > 0.5f && result.mass.center_of_mass.x < 1.5f);
    assert(result.mass.center_of_mass.y > 0.2f && result.mass.center_of_mass.y < 0.8f);
    assert(result.mass.inertia_diagonal.x > 0.0f);
    assert(result.mass.inertia_diagonal.y > 0.0f);
    assert(result.mass.inertia_diagonal.z > 0.0f);
    assert(result.wheel_mounts.size() == 4);
}

void test_voxel_vehicle_builder_deterministic() {
    const std::vector<VoxelMassCell> cells{
        {glm::ivec3(0, 0, 0), VoxelMassMaterial{500.0f}},
        {glm::ivec3(1, 0, 0), VoxelMassMaterial{500.0f}},
        {glm::ivec3(0, 1, 0), VoxelMassMaterial{700.0f}},
        {glm::ivec3(1, 1, 0), VoxelMassMaterial{700.0f}}};

    const VoxelVehicleBuildResult a = build_voxel_vehicle_properties(cells, 1.0f);
    const VoxelVehicleBuildResult b = build_voxel_vehicle_properties(cells, 1.0f);
    assert(std::fabs(a.mass.total_mass_kg - b.mass.total_mass_kg) < 1.0e-5f);
    assert(glm::length(a.mass.center_of_mass - b.mass.center_of_mass) < 1.0e-6f);
    assert(glm::length(a.mass.inertia_diagonal - b.mass.inertia_diagonal) < 1.0e-6f);
}

void test_ground_vehicle_controller_accel_and_brake() {
    VoxelChunk chunk;
    chunk.generate_flat_ground(0);
    VoxelCollisionWorld collision_world(&chunk);

    GroundVehicleController controller;
    controller.reset(glm::vec3(8.0f, 1.4f, 8.0f), 0.0f);

    VehicleControlInput throttle{};
    throttle.throttle = 1.0f;
    uint32_t max_grounded = 0;
    for (int i = 0; i < 60; ++i) {
        controller.step(throttle, collision_world, 1.0f / 60.0f);
        max_grounded = std::max(max_grounded, controller.state().telemetry.grounded_wheels);
    }
    const float speed_after_accel = controller.state().telemetry.speed_mps;
    assert(speed_after_accel > 1.5f);
    assert(max_grounded > 0);

    VehicleControlInput brake{};
    brake.brake = 1.0f;
    for (int i = 0; i < 60; ++i) {
        controller.step(brake, collision_world, 1.0f / 60.0f);
    }
    assert(controller.state().telemetry.speed_mps < speed_after_accel);
}

void test_aircraft_controller_throttle_and_pitch() {
    VoxelChunk chunk;
    chunk.generate_flat_ground(0);
    VoxelCollisionWorld collision_world(&chunk);

    AircraftController controller;
    controller.reset(glm::vec3(8.0f, 7.0f, 8.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 6.0f));

    AircraftControlInput throttle{};
    throttle.throttle = 1.0f;
    for (int i = 0; i < 120; ++i) {
        controller.step(throttle, collision_world, 1.0f / 60.0f);
    }
    const float boosted_speed = controller.state().telemetry.speed_mps;
    assert(boosted_speed > 8.0f);

    AircraftControlInput pitch{};
    pitch.throttle = 0.7f;
    pitch.pitch = 0.4f;
    for (int i = 0; i < 90; ++i) {
        controller.step(pitch, collision_world, 1.0f / 60.0f);
    }
    assert(std::fabs(controller.state().kinematic.euler.x) > 0.01f);
    assert(controller.state().kinematic.position.y > 2.0f);
}

void test_vehicle_drivetrain_shift_behavior() {
    VehicleDrivetrain drivetrain;
    drivetrain.reset();
    float scaled_throttle = 0.0f;

    for (int i = 0; i < 240; ++i) {
        scaled_throttle = drivetrain.update(1.0f, 24.0f, 1.0f / 60.0f);
    }
    assert(drivetrain.telemetry().current_gear >= 2);
    assert(drivetrain.telemetry().engine_rpm > 1500.0f);
    assert(scaled_throttle > 0.1f);
}

void test_vehicle_damage_model_impact() {
    std::vector<VoxelMassCell> cells;
    for (int x = 0; x < 3; ++x) {
        for (int y = 0; y < 2; ++y) {
            for (int z = 0; z < 2; ++z) {
                cells.push_back({glm::ivec3(x, y, z), VoxelMassMaterial{}});
            }
        }
    }

    VehicleDamageModel damage;
    damage.initialize(cells, 100.0f);
    const float integrity_before = damage.stats().integrity;
    damage.apply_impact(glm::vec3(0.5f, 0.5f, 0.5f), 3000.0f, 1.8f);
    const float integrity_after = damage.stats().integrity;
    assert(integrity_after <= integrity_before);
    assert(damage.stats().mass_scale <= 1.0f);
}

void test_vehicle_damage_model_deterministic() {
    const std::vector<VoxelMassCell> cells{
        {glm::ivec3(0, 0, 0), VoxelMassMaterial{}},
        {glm::ivec3(1, 0, 0), VoxelMassMaterial{}},
        {glm::ivec3(0, 1, 0), VoxelMassMaterial{}},
        {glm::ivec3(1, 1, 0), VoxelMassMaterial{}}};

    VehicleDamageModel a;
    VehicleDamageModel b;
    a.initialize(cells, 80.0f);
    b.initialize(cells, 80.0f);

    a.apply_impact(glm::vec3(0.5f, 0.5f, 0.5f), 1000.0f, 1.1f);
    b.apply_impact(glm::vec3(0.5f, 0.5f, 0.5f), 1000.0f, 1.1f);
    assert(std::fabs(a.stats().integrity - b.stats().integrity) < 1.0e-6f);
    assert(a.stats().destroyed_cells == b.stats().destroyed_cells);
}

void test_vehicle_sandbox_scene_step() {
    VehicleSandboxScene sandbox;
    sandbox.init_default();

    InputState vehicle_input{};
    vehicle_input.move = glm::vec2(0.1f, 1.0f);
    InputState aircraft_input{};
    aircraft_input.move = glm::vec2(-0.2f, 0.7f);
    aircraft_input.jump_held = true;

    for (int i = 0; i < 90; ++i) {
        sandbox.step(vehicle_input, aircraft_input, 1.0f / 60.0f);
    }

    const VehicleSandboxSnapshot snap = sandbox.snapshot();
    assert(snap.vehicle_speed_mps > 0.5f);
    assert(snap.aircraft_speed_mps > 6.0f);
    assert(snap.vehicle_position.y > 0.2f);
    assert(snap.aircraft_position.y > 2.0f);
    assert(snap.static_shape_count > 0);
}

}

int main() {
    test_camera_vectors();
    test_net_pod_serialization();
    test_chunk_meshing();
    test_chunk_seed_determinism();
    test_camera_yaw_response();
    test_strafe_axis_sign();
    test_player_settles_on_ground();
    test_player_animation_state_transitions();
    test_avbd_solver_lifecycle();
    test_minigame_snake_runs();
    test_minigame_golf_shot();
    test_minigame_tetris_progress();
    test_minigame_racing_completion();
    test_minigame_tictactoe_places_marks();
    test_web_platform_state();
    test_android_platform_state();
    test_vehicle_foundation_fixed_step_counter();
    test_vehicle_kinematic_determinism();
    test_aircraft_kinematic_determinism();
    test_voxel_physics_bridge_chunk_tracking();
    test_voxel_physics_bridge_shape_build();
    test_voxel_vehicle_builder_mass_properties();
    test_voxel_vehicle_builder_deterministic();
    test_ground_vehicle_controller_accel_and_brake();
    test_aircraft_controller_throttle_and_pitch();
    test_vehicle_drivetrain_shift_behavior();
    test_vehicle_damage_model_impact();
    test_vehicle_damage_model_deterministic();
    test_vehicle_sandbox_scene_step();
    return 0;
}
