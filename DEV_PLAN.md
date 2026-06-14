# Voxov Development Plan — Parallel Agent Tasks

## Current State (2026-06-14)
- ✅ Spherical voxel planet renders (500m radius, 1m blocks)
- ✅ First-person camera with mouse look
- ✅ Walking on sphere surface with gravity
- ✅ Block break/place interaction
- ✅ Face culling fixed (all directions 80-95%)
- ⚠️ No greedy meshing (per-face quads, high vertex count)
- ⚠️ Basic lighting (no shadows, no ambient occlusion)
- ⚠️ No LOD system (same resolution everywhere)
- ⚠️ Limited debug tooling

## Parallel Tasks

### Task 1: Rendering Pipeline (DeepSeek)
**Goal**: Greedy meshing + improved lighting
**Files**: `src/engine_world/planet_blocks.cpp`, `src/engine_render/sokol_renderer.cpp`
**Agent**: `deepseek-implement`

### Task 2: Debug Tooling (Mimo)
**Goal**: Frame profiler, GPU stats, visual debug overlays
**Files**: `src/engine/engine.cpp`, `src/engine_render/sokol_renderer.cpp`
**Agent**: `mimo-v2.5-pro` via opencode

### Task 3: Architecture (DeepSeek)
**Goal**: Proper chunk streaming with mesh caching
**Files**: `src/engine/engine.cpp`, `src/engine_world/planet_blocks.hpp`
**Agent**: `deepseek-implement`

## Isolation Strategy
- Each agent works on separate files where possible
- Build after each change: `cmake --build build/desktop/main --parallel`
- Test after each change: `timeout 5 ./build/desktop/main/bin/voxov`
- Commit after each successful change
