# Engine Reference Guide

The external extracted texts are inputs for the Codex lead architect's analysis.
OpenCode workers do not read or interpret them directly and this guide is not
loaded into their instruction set. Codex extracts concrete requirements
into a task packet after reconciling reference guidance with the existing
source and roadmap. These references do not override `AGENTS.md` or existing
system constraints.

## Reference Library

| Reference file | Consult for |
| --- | --- |
| `/home/mbuthi/Documents/game-dev/extracted_text/Game-Engine-Architecture-3rd-Edition.md` | Runtime subsystem ownership, game/render loop responsibilities, memory and resource management, concurrency/job design, rendering-engine layering, profiling and debugging |
| `/home/mbuthi/Documents/game-dev/extracted_text/Foundations-of-Game-Engine-Development-Vol-1.md` | Vector, matrix, transform, geometry, and numerical reasoning for world/camera/planet math |
| `/home/mbuthi/Documents/game-dev/extracted_text/Foundations-of-Game-Engine-Development-Vol-2-Rendering.md` | Coordinate spaces, camera/projection transforms, graphics pipeline, lighting/materials, visibility and occlusion |
| `/home/mbuthi/Documents/game-dev/extracted_text/Computer-Graphics-Programming-in-OpenGL-with-Cpp.md` | OpenGL-side API patterns and shader/rendering implementation background where compatible with the repository backend |

## VOXOV Mapping

Codex uses the reference library selectively against current code:

| Work area | Source locations to inspect first | Reference emphasis |
| --- | --- | --- |
| Runtime ownership and shrinking orchestration | `src/engine/`, `src/engine_runtime/`, `src/game/` | Runtime architecture and game-loop boundaries |
| Renderer backend and GPU resource lifecycle | `src/engine_render/`, `src/platform/sokol/`, `src/renderer/` | Rendering-engine layers, pipeline state, resource lifetime and visibility |
| Planet coordinate model and LOD terrain | `src/engine_world/planet.*`, `src/engine_world/voxel_chunk.*`, `docs/voxel_planet_roadmap.md` | Coordinate spaces, transforms, spatial subdivision and visibility |
| Collision and traversal | `src/engine_world/physics/`, `src/engine_physics/`, `src/engine_gameplay/player/` | Consistent world representation, fixed-step behavior and physics boundaries |
| Future async streaming | `src/engine_core/jobs.*`, `src/engine_world/` | Job-system ownership, synchronization and bounded main-thread transfer |

## Reading Rule

Codex begins with repository source and the roadmap, then consults only the
reference topics needed for a design decision. When online sources are
needed, Codex finds and evaluates them; workers do not browse for
architecture or rendering guidance. The lead distinguishes source facts from
inferences and converts accepted guidance into concrete VOXOV ownership,
lifetime, coordinate, rendering, or verification requirements in each task
packet.
