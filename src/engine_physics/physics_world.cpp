#include "engine_physics/physics_world.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

#include <thread>

using namespace JPH;

static constexpr uint32_t MAX_BODIES = 1024;
static constexpr uint32_t NUM_BODY_MUTEXES = 0;
static constexpr uint32_t MAX_BODY_PAIRS = 1024;
static constexpr uint32_t MAX_CONTACT_CONSTRAINTS = 1024;

void PhysicsWorld::init(const PhysicsSettings &settings) {
    RegisterDefaultAllocator();
    Factory::sInstance = new Factory();
    RegisterTypes();

    temp_allocator = new TempAllocatorImpl(10 * 1024 * 1024);
    job_system = new JobSystemThreadPool(0, 0, std::thread::hardware_concurrency() - 1);

    auto *system = new PhysicsSystem();
    physics_system = system;
    system->Init(MAX_BODIES, NUM_BODY_MUTEXES, MAX_BODY_PAIRS, MAX_CONTACT_CONSTRAINTS, {}, {});

    system->SetGravity(Vec3(0.0f, settings.gravity, 0.0f));

    BodyInterface &bi = system->GetBodyInterface();
    auto floor = new BoxShape(Vec3(50.0f, 1.0f, 50.0f));
    BodyCreationSettings floor_settings(floor, Vec3(0.0f, -1.0f, 0.0f), Quat::sIdentity(), EMotionType::Static, 0);
    BodyID floor_id = bi.CreateAndAddBody(floor_settings, EActivation::DontActivate);

    auto box = new BoxShape(Vec3(0.5f, 0.5f, 0.5f));
    BodyCreationSettings box_settings(box, Vec3(0.0f, 3.0f, 0.0f), Quat::sIdentity(), EMotionType::Dynamic, 0);
    bi.CreateAndAddBody(box_settings, EActivation::Activate);

    (void)floor_id;
}

void PhysicsWorld::shutdown() {
    delete static_cast<PhysicsSystem *>(physics_system);
    delete static_cast<TempAllocatorImpl *>(temp_allocator);
    delete static_cast<JobSystemThreadPool *>(job_system);
    UnregisterTypes();
    delete Factory::sInstance;
    Factory::sInstance = nullptr;
    physics_system = nullptr;
    temp_allocator = nullptr;
    job_system = nullptr;
}

void PhysicsWorld::step(float dt_seconds) {
    auto *system = static_cast<PhysicsSystem *>(physics_system);
    system->Update(dt_seconds, 1, static_cast<TempAllocator *>(temp_allocator), static_cast<JobSystem *>(job_system));
}
