# Voxel Terrain Rendering Problem — Debug Report

## Project
Minecraft-style game on a spherical voxel planet (500m radius, 1m blocks, cube-sphere with 6 faces, 8 radial shells).

## Build System
- C++23, Sokol renderer (OpenGL/GLES3), CMake
- Build: `cmake --build build/desktop/main --parallel`
- Run: `./build/desktop/main/bin/voxov`

---

## Problem 1: Sokol Uniform Block Warnings

**Symptom:** Every frame produces these warnings:
```
[sg][warning] GL_UNIFORMBLOCK_NAME_NOT_FOUND_IN_SHADER: uniform block name not found in shader
[sg][info] planet_center_radius
[sg][info] atm_params_1
[sg][info] rayleigh_scatter_unused
[sg][info] mie_scatter_pad
[sg][info] sun_dir_intensity
```

**Cause:** The GLSL shader defines a `layout(std140) uniform atm_params { ... }` block, but the GL driver's compiler removes it because the uniforms are unused in the current code path (or the block name doesn't match what sokol expects).

**Shader source (fragment, GL 330):**
```glsl
layout(std140) uniform atm_params {
    vec4 planet_center_radius;      // xyz=planet center, w=radius
    vec4 atm_params_1;              // x=atm_height, y=H_R, z=H_M, w=g
    vec4 rayleigh_scatter_unused;   // xyz=beta_R
    vec4 mie_scatter_pad;           // x=beta_M
    vec4 sun_dir_intensity;         // xyz=sun_dir, w=intensity
};
```

**Sokol uniform block setup (sokol_renderer.cpp):**
```cpp
shd_desc.uniform_blocks[2].stage = SG_SHADERSTAGE_FRAGMENT;
shd_desc.uniform_blocks[2].size = sizeof(atm_params_t);  // 80 bytes
shd_desc.uniform_blocks[2].layout = SG_UNIFORMLAYOUT_STD140;
shd_desc.uniform_blocks[2].glsl_uniforms[0].glsl_name = "planet_center_radius";
// ... etc
```

**The GL driver is removing the uniform block because the GLSL block name `atm_params` doesn't match the individual uniform names sokol is looking for.** Sokol matches uniforms by name inside the block, but the block itself needs to be actively used.

---

## Problem 2: Voxel Terrain Appears Flat (No 3D Height Variation)

**Symptom:** Terrain renders as flat colored rectangles with grid lines. No visible hills, valleys, or individual block depth. Mimo Omni vision model confirms: "terrain appears completely flat with no elevation changes."

**Diagnostics:**
```
Chunk gen: sector=0 shell=7 chunk=(29,1,29) solid=923/4096 base_col=(464, 464) terrain_h=[8..28] vlayers=30
Mesh build: verts=5368 idxs=8052 tris=2684 camera_origin=(523.0,0.0,0.0)
Face counts: +x=170 -x=170 +y=260 -y=260 +z=241 -z=241
LOD distribution: L0=8 L1=0 L2=0 L3=0
```

- Terrain height DOES vary [8..28] blocks
- Face counts ARE balanced (170 up, 260 north/south, 241 east/west)
- 5368 vertices per chunk (per-face meshing, not greedy)
- 49 chunks loaded, 51 opaques in scene

**But visually it looks flat.**

### Key Source Files

**Terrain generation (planet_blocks.cpp):**
```cpp
int32_t SphereNoise3D::terrain_height(const glm::dvec3 &direction,
                                       float base_height, float amplitude) const {
    float h = base_height;  // 15.0
    h += sample(direction, 200.0f) * amplitude * 0.5f;
    h += sample(direction, 80.0f) * amplitude * 0.3f;
    h += sample(direction, 30.0f) * amplitude * 0.2f;
    return std::clamp(static_cast<int32_t>(std::round(h)), 8, 28);
}
```

**Noise sampling (planet_blocks.cpp):**
```cpp
float SphereNoise3D::sample(const glm::dvec3 &direction, float frequency) const {
    return value_noise(direction * static_cast<double>(frequency));
}

float SphereNoise3D::value_noise(const glm::dvec3 &p) const {
    // Integer/fractional split, smoothstep interpolation, hash-based
    const int64_t ix = static_cast<int64_t>(std::floor(p.x));
    const int64_t iy = static_cast<int64_t>(std::floor(p.y));
    const int64_t iz = static_cast<int64_t>(std::floor(p.z));
    const glm::dvec3 f(p.x - ix, p.y - iy, p.z - iz);
    // ... hash and lerp
}
```

**Block position on sphere (planet_blocks.cpp):**
```cpp
// In build_chunk_mesh, per-block:
const double u = -1.0 + (gx + 0.5) / static_cast<double>(sh.horizontal_res) * 2.0;
const double v = -1.0 + (gz + 0.5) / static_cast<double>(sh.horizontal_res) * 2.0;
const double lt = (gy + 0.5) / static_cast<double>(sh.vertical_layers);
const double r = sh.inner_radius + (sh.outer_radius - sh.inner_radius) * lt;
const glm::dvec3 center = planet.center + face_uv_to_direction(face, u, v) * r;
```

**Tangent basis for normals:**
```cpp
const glm::dvec3 radial = glm::normalize(center - planet.center);
const PlanetTangentBasis tb = tangent_basis(radial);
// Face normal = one of: tb.east, tb.north, radial (with sign)
```

**Shell configuration:**
```
inner_radius = 500.0
outer_radius = 530.0
vertical_layers = 30
horizontal_res = 1024
block_size = 1.0
chunk_size = 16
```

**Camera (first-person on sphere):**
```cpp
const glm::vec3 local_up = glm::normalize(render_position);
const glm::vec3 eye_pos = render_position + local_up * 1.6f;
const glm::vec3 view_dir = normalize(
    cos(pitch) * (sin(yaw) * east + cos(yaw) * north) + sin(pitch) * local_up);
const glm::mat4 view_matrix = glm::lookAt(eye_pos, eye_pos + view_dir, local_up);
```

**Camera-relative rendering:**
```cpp
// Vertices stored as offset from snap origin for float32 precision
mesh.vertices.push_back(RenderVertex{
    glm::vec3(world_position - camera_relative_origin),
    color, glm::vec3(normal)});

// In renderer:
const glm::mat4 origin_t = glm::translate(glm::mat4(1.0f), glm::vec3(camera_origin));
const glm::mat4 rel_vp = vp * origin_t;
// Shader: gl_Position = mvp * vec4(position, 1.0)
// = vp * translate(origin) * (world - origin) = vp * world  ✓
```

**Vertex shader:**
```glsl
#version 330
uniform mat4 mvp;
uniform mat4 model;
layout(location=0) in vec3 position;
layout(location=1) in vec3 color0;
layout(location=2) in vec3 normal;
out vec3 v_color;
out vec3 v_normal;
out vec3 v_world_pos;
void main() {
    vec4 world_pos = model * vec4(position, 1.0);
    v_color = color0;
    v_normal = mat3(model) * normal;
    v_world_pos = world_pos.xyz;
    gl_Position = mvp * vec4(position, 1.0);
}
```

**Fragment shader (lighting):**
```glsl
void main() {
    vec3 n = normalize(v_normal);
    vec3 l = normalize(light_direction);
    vec3 v = normalize(camera_pos - v_world_pos);
    vec3 h = normalize(l + v);
    float ndl = max(dot(n, l), 0.0);
    // ... specular ...
    vec3 lit = ambient + diffuse + specular;
    frag_color = vec4(v_color * lit * atm_trans, 1.0);
}
```

---

## Problem 3: Vertex0 Sometimes CLIPPED

**Diagnostics:**
```
Frame 60:  Vertex0: world=(512.0,-48.0,-48.5) clip=(-35.33,-0.74,52.28,53.26) ndc=(-0.66,-0.01,0.98) VISIBLE
Frame 120: Vertex0: world=(512.0,-48.0,-48.5) clip=(-47.64,-31.13,-28.91,-27.90) ndc=(1.71,1.12,1.04) CLIPPED
Frame 180: Vertex0: world=(512.0,-48.0,-48.5) clip=(-53.60,-14.63,13.66,14.65) ndc=(-3.66,-1.00,0.93) CLIPPED
```

Same vertex position but different clip/NDC — camera moved between frames. Some frames vertices are outside [-1,1] NDC range.

---

## What Works
- ✅ Build succeeds
- ✅ Chunks generated with terrain variation [8..28]
- ✅ Meshes built (5368 verts, 2684 tris per chunk)
- ✅ Draw calls execute with valid GPU buffers
- ✅ Face counts balanced (all 6 directions have faces)
- ✅ Player walks on sphere surface
- ✅ First-person camera with mouse look
- ✅ Solar system (sun, planet, moon orbiting)
- ✅ Atmosphere code exists (but warnings suggest not working)

## What Doesn't Work
- ❌ Terrain appears flat (no visible 3D height variation)
- ❌ Individual blocks not clearly visible as 3D cubes
- ❌ Atmosphere uniform block warnings every frame
- ❌ Vertex positions sometimes clipped (camera issue?)

---

## Questions for the AI

1. **Why does terrain look flat despite terrain_h=[8..28] variation?** Is the camera angle wrong? Is the lighting not showing depth? Are the blocks too small to see at this distance?

2. **How to fix the GL_UNIFORMBLOCK_NAME_NOT_FOUND_IN_SHADER warnings?** The shader has `layout(std140) uniform atm_params { ... }` but sokol can't find the block. Is it a naming mismatch between sokol's `glsl_uniforms` and the GLSL block?

3. **Why are vertices sometimes CLIPPED?** The same world-space vertex shows different NDC values across frames. Is the camera-relative origin drifting incorrectly?

4. **Is the vertex shader correct?** `gl_Position = mvp * vec4(position, 1.0)` where `mvp = vp * translate(origin)` and `position = world - origin`. This should reconstruct world position. Is there a precision issue?

5. **Is the lighting correct for a sphere?** The normal is `mat3(model) * normal` where `model = identity`. The normal comes from the tangent basis (east, north, radial). Should the lighting show more contrast between block faces?

---

## Environment
- Linux, OpenGL 3.3 Core
- Sokol renderer (sokol_gfx.h)
- GLM for math
- 500m radius planet, 1m blocks, 16³ chunks
