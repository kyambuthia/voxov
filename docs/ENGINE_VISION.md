# VOXOV Engine Vision

VOXOV is evolving from a learning project into a production-oriented game engine. The vision
is grounded in Jason Gregory's *Game Engine Architecture* and targets a complete, reusable
3D game engine capable of shipping multiplayer games across desktop, mobile, web, and
eventually consoles.

## Core Philosophy

**A game engine is a software development kit for making games.** It must be:

- **Reusable**: The same engine core builds different games without major modification.
- **Data-driven**: Artists and designers control content and behavior without programmer
  intervention.
- **Layered**: Upper subsystems depend on lower ones; never the reverse.
- **Performant**: Fixed-step simulation, custom allocators, cache-conscious data layout.
- **Debuggable**: First-class profiling, visualization, and introspection tools built in.

## Product Goals

### Playable
- Input latency under 16ms (60 FPS response) on desktop.
- Camera and controls feel responsive across all traversal modes.
- Stable frame pacing with no hitches during streaming or loading.

### Visual
- Clean physically-based shading with consistent material response.
- Efficient voxel terrain rendering with greedy meshing and LOD.
- Atmospheric scattering, dynamic shadows, and ambient occlusion.

### Multiplayer
- Authoritative dedicated server with client-side prediction.
- Snapshot interpolation for smooth remote player rendering.
- Consistent simulation across platforms (server-side truth, client-side
  deterministic generation from shared seeds).

### Cross-Platform
- One shared C++ runtime (`GameRuntime`) across all targets.
- Thin platform adapters for windowing, input, rendering context.
- Mobile-friendly resource budgets (GLES3/WebGL2 ceiling).

### Extensible
- Component-based game object model with data-driven entity definitions.
- Event-driven communication between subsystems.
- Scriptable gameplay behavior (Lua or similar).
- Hot-reloadable assets and scripts during development.

## Design Philosophy (from GEA)

### 1. Data-Driven Architecture (GEA §15.3)
The engine should be a general-purpose toolkit. Everything specific to a game — character
stats, world layout, enemy behavior, dialog — should live in data and scripts, not in
compiled C++. This is what separates a "game engine" from "a game."

### 2. Component-Based Object Model (GEA §16.2)
Game objects are containers of components, not leaves in an inheritance tree. Deep class
hierarchies are brittle and resist change. Components are independently maintainable,
testable, and reusable across different entity types.

### 3. Bulk Subsystem Updates (GEA §16.6)
Subsystems update all instances of a component type together, rather than iterating per-object
and calling subsystem methods inside `Update()`. This improves cache coherence, avoids
redundant computation, and enables efficient work distribution across CPU cores.

### 4. Fixed-Step Simulation (GEA §8.3)
Physics and gameplay update at a fixed time step (e.g., 60 Hz), independent of the render
frame rate. This ensures deterministic behavior, stable physics, and consistent multiplayer
simulation. The renderer interpolates or extrapolates visual state between simulation ticks.

### 5. Resource Manager (GEA §7.2)
All assets (meshes, textures, animations, sounds, scripts) are managed through a unified
resource system. The resource manager ensures single-instance storage, reference-counted
lifetimes, and automatic cross-reference resolution. No subsystem loads files directly.

### 6. Explicit Lifecycle Management (GEA §6.1)
Subsystem startup and shutdown happen in explicit code, in a known order. Global
constructors and destructors are avoided. This makes initialization predictable and
debuggable — if something starts at the wrong time, you move one line of code.

### 7. Custom Memory Management (GEA §6.2)
The general-purpose heap is too slow and causes fragmentation. Custom allocators — stack
for level loads, pool for fixed-size objects, single-frame for per-tick temporaries —
give deterministic performance. Heap allocation in tight loops is a bug.

### 8. Middleware for Commodity Problems (GEA §1.6.4)
Physics, collision detection, audio mixing, and model loading are solved problems. Use
proven libraries (Jolt, ENet, miniaudio, cgltf). Invest custom engineering effort only
where it differentiates your game: gameplay, rendering style, unique mechanics.

### 9. Tools as First-Class Products (GEA §1.7)
The asset pipeline, world editor, and debugging tools are not afterthoughts — they
determine how fast the team can iterate. A good world editor is worth more than any
single rendering feature. Tools don't need to be pretty, but they must be reliable
and fast.

## Non-Goals (Never, or Not Yet)

- **Massive open worlds**: Current scope is bounded planet-sized worlds with LOD.
- **MMO-scale networking**: Up to 32 players, co-op and competitive, not thousands.
- **Full editor suite**: Focus on runtime first; editor work is deferred until the
  engine core is stable.
- **Multi-backend renderer**: Stay on OpenGL/GLES3/WebGL2. Vulkan is removed. The
  simplicity of a single backend is more valuable than hypothetical performance gains
  from multi-backend abstraction.

## Traversal Vision

VOXOV aims for three first-class traversal modes, all networked:

1. **On-foot**: Walking, running, jumping, crouching on planetary surfaces. Procedural
   locomotion with IK foot placement on uneven voxel terrain.
2. **Ground vehicles**: Cars and wheeled vehicles with drivetrain simulation, suspension,
   and damage modeling.
3. **Aircraft**: Atmospheric flight and interplanetary travel. Planet descent/ascent
   with seamless LOD transitions.

Traversal is the central gameplay loop — everything else (combat, collection, construction)
hangs off the movement system.

## Platform Ambition

| Platform | Priority | Rationale |
|---|---|---|
| Linux desktop | Primary | Development host, CI, release |
| Windows desktop | Primary | Largest player base |
| Android | High | Mobile multiplayer, large install base |
| Web (WASM) | Medium | Zero-install distribution, browser play |
| macOS | Medium | If developer demand justifies it |
| iOS | Deferred | After shared runtime convergence |
| Consoles | Deferred | After all other targets stable |
| XR | Research | Experimental scaffold only |

## What Success Looks Like

Two clients connect to a server, move around a voxel planet, drive vehicles, fly aircraft,
see each other smoothly, and interact with shared world state — all from the same C++
codebase compiled for desktop, mobile, and web. The engine is modular enough that
someone could use the voxel renderer for a Minecraft-like, or swap in a different
world representation and build a shooter, all while reusing the networking, physics,
animation, and audio subsystems.

This is the vision. The roadmap tells us how to get there.
