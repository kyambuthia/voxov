#include "engine_physics/physics_world.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/RegisterTypes.h>

#include <thread>

using namespace JPH;

static constexpr uint32_t MAX_BODIES = 1024;
static constexpr uint32_t NUM_BODY_MUTEXES = 0;
static constexpr uint32_t MAX_BODY_PAIRS = 1024;
static constexpr uint32_t MAX_CONTACT_CONSTRAINTS = 1024;

namespace {
    static constexpr ObjectLayer LAYER_STATIC = 0;
    static constexpr ObjectLayer LAYER_DYNAMIC = 1;

    class BroadPhaseLayerInterfaceImpl : public BroadPhaseLayerInterface {
    public:
        BroadPhaseLayerInterfaceImpl() {
            layers[0] = BroadPhaseLayer(0);
            layers[1] = BroadPhaseLayer(1);
        }

        uint GetNumBroadPhaseLayers() const override {
            return 2;
        }

        BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override {
            return layers[inLayer];
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char *GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override {
            switch (inLayer.GetValue()) {
            case 0: return "Static";
            case 1: return "Dynamic";
            default: return "Unknown";
            }
        }
#endif

    private:
        BroadPhaseLayer layers[2];
    };

    class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter {
    public:
        bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override {
            if (inLayer1 == LAYER_STATIC) {
                return inLayer2.GetValue() == 1;
            }
            return true;
        }
    };

    class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter {
    public:
        bool ShouldCollide(ObjectLayer inLayer1, ObjectLayer inLayer2) const override {
            if (inLayer1 == LAYER_STATIC && inLayer2 == LAYER_STATIC) {
                return false;
            }
            return true;
        }
    };
}

void PhysicsWorld::init(const EnginePhysicsSettings &settings) {
    RegisterDefaultAllocator();
    Factory::sInstance = new Factory();
    RegisterTypes();

    static BroadPhaseLayerInterfaceImpl broad_phase_layer_interface;
    static ObjectVsBroadPhaseLayerFilterImpl object_vs_broadphase;
    static ObjectLayerPairFilterImpl object_layer_pair;

    temp_allocator = new TempAllocatorImpl(10 * 1024 * 1024);
    const uint32_t hw_threads = std::thread::hardware_concurrency();
    const uint32_t worker_threads = hw_threads > 1 ? (hw_threads - 1) : 1;
    constexpr uint32_t max_jobs = 1024;
    constexpr uint32_t max_barriers = 1024;
    job_system = new JobSystemThreadPool(max_jobs, max_barriers, worker_threads);

    auto *system = new PhysicsSystem();
    physics_system = system;
    system->Init(
        MAX_BODIES,
        NUM_BODY_MUTEXES,
        MAX_BODY_PAIRS,
        MAX_CONTACT_CONSTRAINTS,
        broad_phase_layer_interface,
        object_vs_broadphase,
        object_layer_pair
    );

    system->SetGravity(Vec3(0.0f, settings.gravity, 0.0f));

    BodyInterface &bi = system->GetBodyInterface();
    auto floor = new BoxShape(Vec3(50.0f, 1.0f, 50.0f));
    BodyCreationSettings floor_settings(floor, Vec3(0.0f, -1.0f, 0.0f), Quat::sIdentity(), EMotionType::Static, LAYER_STATIC);
    BodyID floor_id = bi.CreateAndAddBody(floor_settings, EActivation::DontActivate);

    auto box = new BoxShape(Vec3(0.5f, 0.5f, 0.5f));
    BodyCreationSettings box_settings(box, Vec3(0.0f, 3.0f, 0.0f), Quat::sIdentity(), EMotionType::Dynamic, LAYER_DYNAMIC);
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
