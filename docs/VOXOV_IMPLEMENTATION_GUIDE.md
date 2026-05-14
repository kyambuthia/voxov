# VOXOV Implementation Guide

This document provides practical implementation guidance for each subsystem, following the
architecture defined in `ARCHITECTURE.md` and the phases laid out in `ROADMAP.md`. Each
section describes the current state, the target state, and the concrete steps to get there.

---

## Phase 1: Foundation Hardening

### 1.1 Memory Management

**Current**: Ad-hoc `new`/`malloc` calls mixed throughout. No custom allocators. The
`engine_core/memory.cpp` provides aligned allocation but no allocator framework.

**Target**: Four custom allocators following GEA §6.2:

```
class StackAllocator {
    void* m_base;
    size_t m_size;
    size_t m_marker;  // current allocation head

    void* alloc(size_t bytes);          // O(1), bump pointer
    Marker getMarker() const;           // save position
    void freeToMarker(Marker m);        // O(1), LIFO rollback
    void clear();                       // reset to base
};

class PoolAllocator {
    void* m_base;
    size_t m_elementSize;
    size_t m_capacity;
    void* m_freeList;                   // embedded free list (next ptr in free block)

    void* alloc();                      // O(1), pop from free list
    void free(void* p);                 // O(1), push to free list
};

class SingleFrameAllocator : StackAllocator {
    void beginFrame();                  // reset to base
    // alloc() works as StackAllocator — cleared at top of next frame
};

class DoubleBufferedAllocator {
    SingleFrameAllocator m_buffers[2];
    int m_current;

    void* alloc(size_t bytes);          // allocate from current buffer
    void swap();                        // switch buffers, clear new current
};
```

**Implementation steps**:
1. Add allocator classes to `src/engine_core/`.
2. Add `AllocJanitor` RAII wrapper that pushes allocator on construction and pops on destruction.
3. Thread-local allocator stack for context-aware allocation (`AllocatorContext`).
4. Replace heap allocations in tight loops with single-frame allocator.
5. Use stack allocator for level/world chunk loads (load on top of stack, free to marker on unload).
6. Use pool allocator for fixed-size objects: matrix palettes, mesh instances, event objects.

**Verification**: Allocator unit tests (alloc, free, fragmentation immunity, alignment).
Profile before/after on desktop and Android.

### 1.2 Hashed String IDs

**Current**: Raw strings used for most identifiers. `strcmp` in animation clip lookups,
material names, and network type identifiers.

**Target**: 64-bit hashed string IDs with compile-time hashing:

```cpp
// Compile-time hashing via constexpr
using StringId = uint64_t;

constexpr StringId fnv1a_64(const char* str, size_t len);

// User-defined literal for C++11
constexpr StringId operator"" _sid(const char* str, size_t len) {
    return fnv1a_64(str, len);
}

// Runtime interning (debug builds only)
class StringTable {
    std::unordered_map<StringId, std::string> m_table;
public:
    StringId intern(const char* str);  // hash + store in debug table
    const std::string& lookup(StringId id) const;  // debug-only reverse lookup
};

// Usage
static constexpr StringId SID_WALK = "walk"_sid;
if (clipId == SID_WALK) { /* O(1) integer compare */ }
```

**Implementation steps**:
1. Add `StringId` type and `operator"" _sid` to `src/engine_core/`.
2. Add `StringTable` for debug-only reverse lookup.
3. Convert animation clip names, network message types, and config keys to use StringId.
4. Intern dynamic strings at static init time; never call `intern()` in a tight loop.
5. Strip string table from shipping builds.

**GEA note**: Raw strings remain for file paths and debug logging only. All runtime
comparisons use integer StringId.

### 1.3 Engine Configuration (Console Variables)

**Current**: CLI flags only. No runtime configuration system. No in-game console.

**Target**: Console variable (cvar) system following Quake's model:

```cpp
enum class CvarType { Bool, Int, Float, String };

struct Cvar {
    StringId        name;
    CvarType        type;
    int             flags;      // CVAR_ARCHIVE, CVAR_CHEAT, CVAR_READONLY
    union {
        bool        b;
        int         i;
        float       f;
    } value;
    const char*     description;
    Cvar*           next;       // global linked list
};

Cvar* Cvar_Find(StringId name);
void  Cvar_Set(StringId name, float val);
float Cvar_GetFloat(StringId name);

// Registration macro
#define CVAR(name, defaultVal, flags, desc) \
    static Cvar cvar_##name = { #name##_sid, CvarType::Float, flags, {.f = defaultVal}, desc, nullptr }
```

**Usage**:
```cpp
CVAR(r_ambient, 0.1f, CVAR_ARCHIVE, "Ambient light intensity");
float ambient = Cvar_GetFloat("r_ambient"_sid);
Cvar_Set("r_ambient"_sid, 0.2f);
```

**In-game console**: Dear ImGui text input + command history + auto-complete. Commands
are registered functions that operate on cvars or trigger actions.

**Implementation steps**:
1. Add cvar system to `src/engine_core/`.
2. Wire command-line parsing (`--cvar value`) to set cvars at startup.
3. Add JSON config file loading/saving (archive-flagged cvars).
4. Build Dear ImGui console window with command history and auto-complete.
5. Convert existing CLI flags to cvars.

### 1.4 Subsystem Startup/Shutdown

**Current**: Mixed initialization — some in global constructors, some in `main()`, some
in `Engine::init()`. Startup order is implicit and fragile.

**Target**: Explicit `startUp()`/`shutDown()` on every subsystem manager:

```cpp
class MemoryManager { public: void startUp(); void shutDown(); };
class FileSystem   { public: void startUp(); void shutDown(); };
class ResourceMgr  { public: void startUp(); void shutDown(); };
class RenderMgr    { public: void startUp(); void shutDown(); };
class PhysicsMgr   { public: void startUp(); void shutDown(); };
class AudioMgr     { public: void startUp(); void shutDown(); };
class NetworkMgr   { public: void startUp(); void shutDown(); };
class InputMgr     { public: void startUp(); void shutDown(); };

int main(int argc, char* argv[]) {
    // Startup: bottom-up (dependents after dependencies)
    gMemoryMgr.startUp();
    gFileSystem.startUp();
    gConfig.parseCommandLine(argc, argv);
    gResourceMgr.startUp();
    gRenderMgr.startUp();
    gPhysicsMgr.startUp();
    gAudioMgr.startUp();
    gNetworkMgr.startUp();
    gInputMgr.startUp();

    gGameRuntime.startUp();
    gGameRuntime.run();

    // Shutdown: top-down (reverse order)
    gGameRuntime.shutDown();
    gInputMgr.shutDown();
    gNetworkMgr.shutDown();
    gAudioMgr.shutDown();
    gPhysicsMgr.shutDown();
    gRenderMgr.shutDown();
    gResourceMgr.shutDown();
    gFileSystem.shutDown();
    gMemoryMgr.shutDown();
}
```

**Implementation steps**:
1. Give each manager empty constructor/destructor + `startUp()`/`shutDown()` methods.
2. Define managers as global singletons (plain variables, not lazy-init).
3. Call in explicit dependency order from `main()`.
4. Remove all global constructors that depend on initialization order.

### 1.5 Refactor Engine Class

**Current**: `Engine` owns rendering, physics, networking, UI, input, gameplay, audio, and
debugging in one monolithic class.

**Target**: `Engine` becomes a thin coordinator. Each subsystem gets its own manager class.

**Extract from `Engine`**:
- `RenderManager`: owns renderer lifecycle, frame begin/end, scene upload
- `PhysicsManager`: owns Jolt world, stepping, collision queries
- `NetworkManager`: owns NetClient/NetServer lifecycle, LAN discovery
- `InputManager`: owns input capture, action mapping, chord detection
- `AudioManager`: owns miniaudio engine, sound banks, voice management
- `UIManager`: owns Dear ImGui context, menu rendering
- `GameplayManager`: owns player controllers, animation, minigames
- `WorldManager`: owns chunk management, world generation, streaming

**Implementation steps**:
1. Create a new manager class for each subsystem under appropriate `src/engine_*/`.
2. Move ownership of subsystem data from `Engine` to the new manager.
3. `Engine::tick()` becomes: `InputMgr.capture() → NetworkMgr.pump() → PhysicsMgr.step() → GameplayMgr.update() → RenderMgr.render() → AudioMgr.update()`.
4. No feature regression. Existing tests continue to pass.
5. Remove the old monolithic `Engine` method calls.

---

## Phase 2: Resource Manager

**Current**: Assets loaded ad-hoc. Each subsystem opens files directly. No lifetime management.
No cross-reference resolution. No caching (except implicit OS file cache).

**Target**: Unified resource manager following GEA §7.2:

```cpp
class ResourceManager {
    struct ResourceEntry {
        StringId    guid;
        void*       data;
        uint32_t    refCount;
        uint16_t    flags;          // RES_GLOBAL, RES_STREAMING, RES_PENDING
        uint16_t    typeId;         // RES_TYPE_TEXTURE, RES_TYPE_MESH, etc.
    };

    HashMap<StringId, ResourceEntry*> m_registry;

    template<typename T>
    T* load(StringId guid);             // increment refcount, load if needed

    void unload(StringId guid);         // decrement refcount, free if zero

    void loadLevel(StringId levelId);   // bulk load level resources
    void unloadLevel(StringId levelId); // bulk unload level resources
};
```

**Lifetime tiers**:
- **Global**: Loaded at engine init, never unloaded. (Core UI textures, font, error model)
- **Level**: Loaded/unloaded with world chunks. (Terrain textures, sound banks, level geometry)
- **Temporary**: Freed when refcount reaches zero. (Generated textures, one-shot effects)

**File system abstraction**:
```cpp
class FileSystem {
    virtual bool exists(const char* path) = 0;
    virtual std::vector<uint8_t> readAll(const char* path) = 0;
    virtual bool writeAll(const char* path, const void* data, size_t size) = 0;

    // Platform backends: POSIX, Win32, Android Assets, Emscripten virtual FS
};
```

**Implementation steps**:
1. Add `FileSystem` base + platform backends to `src/platform/`.
2. Add `ResourceManager` to `src/engine_core/` or new `src/engine_resources/`.
3. Implement GUID registry with hash map.
4. Implement reference counting with level scoping.
5. Convert texture loading, model loading, audio loading to go through ResourceManager.
6. Add resource status debugging (memory usage, refcounts, load times).

---

## Phase 4: Rendering Engine Overhaul

### 4.1 Graphics Device Interface

```cpp
class GraphicsDevice {
    // Buffer management
    virtual BufferHandle createVertexBuffer(size_t size, const void* data) = 0;
    virtual BufferHandle createIndexBuffer(size_t size, const void* data) = 0;
    virtual void updateBuffer(BufferHandle h, size_t offset, size_t size, const void* data) = 0;

    // Texture management
    virtual TextureHandle createTexture2D(int w, int h, int mipLevels, PixelFormat fmt, const void* data) = 0;

    // Shader management
    virtual ShaderHandle compileShader(const char* vertexSrc, const char* fragmentSrc) = 0;

    // Draw calls
    virtual void draw(DrawCommand cmd) = 0;
};

class GLGraphicsDevice : public GraphicsDevice {
    // OpenGL/GLES3 implementation of the above
};
```

### 4.2 Material System

```cpp
struct Material {
    ShaderHandle shader;
    struct Parameter {
        StringId name;
        enum Type { Float, Vec3, Vec4, Texture };
        Type type;
        union { float f; glm::vec3 v3; glm::vec4 v4; TextureHandle tex; } value;
    };
    std::vector<Parameter> parameters;

    // GPU state
    bool depthTest;
    bool depthWrite;
    bool alphaBlend;
    GLenum blendSrc, blendDst;
    GLenum cullMode;
};

using MaterialHandle = StringId;
```

Materials are defined in JSON files and reference shader programs and textures by StringId.
At load time, parameters are bound to shader uniform locations.

### 4.3 Scene Graph: Octree for Voxel World

Since VOXOV's world is a regular 3D grid of chunks, an octree is the natural spatial
subdivision:

```cpp
struct OctreeNode {
    AABB bounds;
    std::vector<MeshInstance> instances;  // for leaf nodes
    std::unique_ptr<OctreeNode> children[8];  // for internal nodes
    bool isLeaf;
};

class OctreeSceneGraph {
    OctreeNode m_root;

    void insert(MeshInstance mesh);
    void remove(MeshInstance mesh);
    std::vector<MeshInstance> queryVisible(const Frustum& frustum);
};
```

### 4.4 Render Pipeline

The canonical frame render order:

```cpp
void RenderManager::renderFrame() {
    // 1. Visibility determination (CPU)
    auto visible = m_sceneGraph.queryVisible(m_camera.frustum());

    // 2. Z-prepass (GPU)
    m_device.setDepthOnly(true);
    sortFrontToBack(visible);
    for (auto& mesh : visible) m_device.draw(mesh);
    m_device.setDepthOnly(false);

    // 3. Opaque pass (GPU)
    sortByMaterial(visible);
    for (auto& mesh : visible) {
        m_device.bindMaterial(mesh.material);
        m_device.draw(mesh);
    }

    // 4. Sky pass
    m_device.bindMaterial(m_skyMaterial);
    m_device.draw(m_skyMesh);

    // 5. Transparent pass
    sortBackToFront(m_transparentObjects);
    m_device.setBlend(true);
    for (auto& obj : m_transparentObjects) m_device.draw(obj);
    m_device.setBlend(false);

    // 6. Post effects
    applyPostEffects();  // bloom, tone mapping, gamma correction

    // 7. HUD / overlays (via Dear ImGui or custom)
    renderHUD();
}
```

---

## Phase 5: Animation System

### 5.1 Data Structures

```cpp
struct Joint {
    StringId    name;
    int32_t     parentIndex;    // -1 for root
    glm::mat4   invBindPose;    // cached, never recomputed at runtime
};

struct Skeleton {
    std::vector<Joint> joints;  // depth-first order (child after parent)
};

struct JointPose {
    glm::quat   rotation;       // local rotation
    glm::vec3   translation;    // local translation
    float       scale;          // uniform scale (non-uniform uses vec3)
};

struct Pose {
    std::vector<JointPose> localPoses;  // one per joint
};

struct AnimationSample {
    std::vector<JointPose> jointPoses;  // one per joint
};

struct AnimationClip {
    float                       duration;
    float                       framesPerSecond;
    bool                        isLooping;
    std::vector<AnimationSample> samples;
};
```

### 5.2 Animation Pipeline

```cpp
void AnimationSystem::evaluate(float deltaTime) {
    // 1. Update animation state machines
    for (auto& entity : m_animatedEntities) {
        entity.asm.update(deltaTime);
    }

    // 2. Evaluate blend trees → per-entity local pose
    for (auto& entity : m_animatedEntities) {
        entity.localPose = entity.asm.evaluate();
    }

    // 3. Generate global poses (hierarchy walk)
    for (auto& entity : m_animatedEntities) {
        entity.globalPose = computeGlobalPose(entity.localPose, entity.skeleton);
    }

    // 4. Post-processing (IK, ragdoll, procedural)
    for (auto& entity : m_animatedEntities) {
        postProcess(entity);
        // Recompute global pose if post-processing modified local pose
        entity.globalPose = computeGlobalPose(entity.localPose, entity.skeleton);
    }

    // 5. Generate matrix palette for skinning
    for (auto& entity : m_animatedEntities) {
        for (size_t j = 0; j < entity.skeleton.joints.size(); ++j) {
            entity.matrixPalette[j] = entity.skeleton.joints[j].invBindPose
                                    * entity.globalPose.jointTransforms[j];
        }
    }
}
```

---

## Phase 6-7: Physics & Audio

### Physics Integration

- **Collision queries**: Ray casts, shape casts, and phantoms exposed through `PhysicsManager`.
  Gameplay systems query these, never accessing Jolt directly.
- **Ragdolls**: `RagdollComponent` attached to entities. On death/knockback: set ragdoll pose
  from animation, apply impulse, let physics simulate. On recovery: drive ragdoll toward
  animation pose via powered constraints, blend back.
- **Vehicles**: Ray cast suspension, apply forces at contact points, simulate drivetrain.
  Separate `VehicleComponent` with wheel/engine/damage sub-components.

### Audio

- **Voice bus**: `AudioManager` maintains a pool of voices. Each voice: play clip → apply gain
  → apply filter → apply pan → mix to output.
- **3D spatialization**: Distance attenuation between min/max radius. Constant-power pan law.
  Listener position = camera position.
- **Sound banks**: Load bank = load all clips + cues into memory. Unload bank = free.
  Global bank always resident. Level banks loaded/unloaded with world chunks.
- **Gameplay triggers**: Animation events → footstep sounds. Collision events with material
  types → impact sounds. UI events → click/move sounds.

---

## Phase 8: Networking Hardening

### Transport Abstraction

```cpp
class ITransport {
public:
    virtual bool listen(uint16_t port) = 0;
    virtual bool connect(const char* host, uint16_t port) = 0;
    virtual void disconnect() = 0;
    virtual void send(const void* data, size_t size, bool reliable) = 0;
    virtual bool receive(void* buffer, size_t& size, bool& reliable) = 0;
    virtual void pump() = 0;
};

class ENetTransport : public ITransport { /* ENet implementation */ };
class WebRTCTransport : public ITransport { /* WebRTC DataChannel */ };
```

### Interest Management

Only send entity state to clients that care about that entity:
- **Distance-based**: Entities beyond a radius are not sent.
- **Relevance-based**: Entities in other game modes (different planet, different minigame) are
  not sent.
- **Frustum-based**: Entities outside the client's view frustum may be sent at lower rate or
  with position-only updates.

---

## Phase 9: Gameplay Foundation

### Component-Based Object Model

```cpp
using EntityId = uint32_t;

struct TransformComponent {
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;
};

struct MeshComponent {
    MeshHandle mesh;
    MaterialHandle material;
};

struct RigidBodyComponent {
    BodyHandle body;  // opaque handle into PhysicsManager
};

struct AnimationComponent {
    SkeletonHandle skeleton;
    ASMHandle stateMachine;
};

struct AudioComponent {
    std::vector<SoundHandle> activeSounds;
};

class Entity {
    EntityId id;
    StringId name;
    TransformComponent* transform;  // always present
    std::vector<Component*> components;
};

class EntityManager {
    std::vector<Entity> m_entities;
    HashMap<EntityId, size_t> m_lookup;

    EntityId create(const EntityDefinition& def);
    void destroy(EntityId id);

    template<typename T> T* getComponent(EntityId id);
    template<typename T> void addComponent(EntityId id, T* comp);
};
```

### Event System

```cpp
struct Event {
    StringId    type;           // hashed event type string
    EntityId    sender;
    VariantMap  parameters;     // key-value parameter storage
};

class EventManager {
    std::vector<Event> m_queues[2];  // double-buffered: one for current frame, one for next

    void send(const Event& e);                       // queue for next frame
    void sendImmediate(const Event& e);              // process now (avoid for complex chains)
    void dispatch();                                  // swap buffers, deliver all pending

    void registerHandler(StringId eventType, EntityId entity, HandlerFn fn);
};
```

### High-Level Game Flow

```cpp
enum class GameState {
    MainMenu,
    Loading,
    Playing,
    Paused,
    Disconnected,
    GameOver
};

class GameFlowFSM {
    GameState m_state;

    void transitionTo(GameState target);
    void onEnter(GameState state);
    void onExit(GameState state);
    void update(float dt);
};
```

---

## Execution Order

This is the canonical per-frame update order for the VOXOV engine loop:

```
1.  InputManager::capture()         — poll HID, update input state
2.  InputManager::applyBindings()   — map raw input to game actions
3.  NetworkManager::pump()          — receive packets, process events
4.  NetworkManager::ingestState()   — apply snapshot corrections, update remote entities
5.  CvarManager::processConsole()   — handle in-game console input
6.  GameFlowFSM::update()           — process game state transitions
7.  PhysicsManager::step(fixedDt)   — advance physics by one fixed step
8.  GameplayManager::preAnimUpdate() — update gameplay state before animation
9.  AnimationSystem::evaluate()     — evaluate blend trees, generate poses
10. GameplayManager::postAnimUpdate() — IK, ragdoll, procedural post-processing
11. RenderManager::visibility()     — frustum culling, occlusion culling
12. RenderManager::renderFrame()    — z-prepass → opaque → sky → transparent → post → HUD
13. AudioManager::update()          — update 3D audio sources, mix
14. DebugOverlay::render()          — draw debug lines, text, stats
15. RenderManager::swapBuffers()    — present frame
16. MemoryManager::endFrame()       — clear single-frame allocator
```

---

## Verification Strategy

Every phase must be verified before proceeding:

1. **Unit tests**: Core systems, math, protocol serialization, allocators — must pass.
2. **Integration tests**: Subsystem interactions (physics + animation, networking + gameplay).
3. **Smoke tests**: Desktop launch, connect two clients, see each other move. Android launch,
   join LAN. Web start, render frame.
4. **Performance baseline**: Frame time, memory, network bandwidth logged and compared against
   previous phase.
5. **CI coverage**: Every phase adds CI jobs for new subsystems. Never remove existing coverage.

**Never ship a phase without validation.** Each phase should produce a runnable, testable
artifact. The engine should always be in a working state — there is no "it will work when
everything is done" phase.
