# AGENTS.md — VOXOV Planet Development Instructions

## Primary Objective
Build a playable Minecraft-style game on a spherical voxel planet at 2000km radius
with ~1m block resolution. The planet must render at interactive frame rates and
the player must be able to walk on the surface.

## Technical Constraints
- C++23, Sokol renderer (OpenGL/GLES3), CMake build
- Cube-sphere (6-face quad sphere) architecture
- Blocks organized in shells (doubling resolution per axis)
- All commits must be small, focused, and buildable
- Never break the build; verify with `cmake --build build/desktop/main --parallel`

## Development Workflow

### Before every change
1. Read the relevant source files to understand current state
2. Search the internet for prior art and proven techniques
3. Inspect dependency source code (dependencies/) for correct API usage
4. Reason from first principles — what must be true for this to work?

### During implementation
1. Keep commits small — one logical change per commit
2. Build after every commit: `cmake --build build/desktop/main --parallel`
3. Run quick smoke test: `timeout 5 ./build/desktop/main/bin/voxov`
4. Add technical comments explaining WHY, not WHAT

### Code review
After meaningful changes, have Xiaomi Mimo (provider: xiamo) from the opencode
CLI critique/review/improve the code. Use:
```
opencode --model "xiaomi/mimo" review <files>
```

## Rendering Performance Targets
- Target: 30+ FPS at surface with 25 loaded chunks
- Must render 3D noise terrain with per-column height variation
- Face culling via neighbor solid-at queries
- Greedy meshing of same-material adjacent faces
- Camera-relative vertex submission for GPU float precision
- Frustum culling: skip chunk mesh generation for chunks outside view

## Debugging Checklist
When surface is not visible:
1. Check `streamed_chunk_count` in HUD (F2 devhud) — should be >0
2. Verify `block_world_.get_or_generate_chunk()` is called
3. Check `build_chunk_mesh()` returns non-empty meshes
4. Verify `scene.opaque_meshes` has entries
5. Confirm camera position is on the correct side of the planet
6. Check GPU upload path in sokol_renderer for mesh_id caching

When player can't move:
1. Verify `debug_fly_mode_` is true (fly mode bypasses collision)
2. Check that `PlayerControllerSystem::simulate_fixed()` is called
3. Confirm WASD input reaches `gameplay_input.move`
4. Verify camera rig yaw/pitch are being updated from mouse

## Internet Research Sources
- Inigo Quilez: iquilezles.org (cube-sphere, noise, domain warping)
- Bowerbyte: bowerbyte.com/posts/blocky-planet (Minecraft spherical planet)
- 0fps.net: voxel meshing algorithms, greedy meshing
- Reddit r/VoxelGameDev: production voxel engine techniques
- Google searches: "voxel planet rendering performance", "greedy meshing sphere",
  "camera relative rendering large worlds", "OpenGL mesh instancing voxels"
