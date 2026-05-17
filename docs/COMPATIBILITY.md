# Voxov Compatibility Targets

Voxov should stay inside a conservative runtime budget so the same game can run
on older PCs, Android, and WebGL2 browsers.

## Baseline

| Target | Baseline |
| --- | --- |
| Older desktop Linux/Windows | OpenGL 3.3-class GPU or D3D11-class GPU |
| Android | OpenGL ES 3.x |
| Web | WebGL2 / GLES3-class feature set |

## Rules

- Do not require desktop OpenGL 4.x features for core gameplay rendering.
- Keep shader source compatible with GLSL 330 and GLES 300 ES unless there is a
  gated fallback.
- Treat Dear ImGui and other debug UI as optional. Core rendering must boot
  without `sokol_imgui`; its embedded GL shader currently requires GLSL 4.10.
- Keep render assets within GLES3/WebGL2 limits: no compute shaders, no desktop
  geometry/tessellation stages, and no mandatory high-sample MSAA.
- Prefer runtime capability gates over hard failure when a debug or quality
  feature is unavailable.

## Current Notes

- Linux Sokol startup requests GL 3.3 explicitly because Sokol defaults GLCORE
  to 4.3 on Linux.
- The Sokol renderer inline smoke shader uses GLSL 330 on desktop GL and GLSL
  300 ES on GLES.
- The existing Android and Web entrypoints remain active compatibility targets
  while the shared Sokol runtime converges.
