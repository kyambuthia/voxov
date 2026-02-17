## VOXOV
Voxel exploration engine foundation (C++23), renderer-agnostic gameplay path, Vulkan + OpenGL backends.

## Status
`WIP` (work in progress). This project is actively evolving and APIs, runtime behavior, and platform support can change between commits.

Current notes:
- Vulkan path is under active stabilization (validation-clean sync and swapchain behavior still being improved).
- Some CPUs may require disabling aggressive Jolt SIMD options (e.g. AVX2/F16C/FMADD/LZCNT) to avoid illegal-instruction startup failures.

## Current Showcase
Latest screenshots:

![VOXOV Screenshot 1](docs/media/voxov_state_01.png)
![VOXOV Screenshot 2](docs/media/voxov_state_02.png)

Latest gameplay clip (GIF from latest screencast):

![VOXOV Gameplay GIF](docs/media/voxov_state.gif)

## Documentation

- Setup and build: `docs/SETUP.md`
- Running and testing: `docs/RUNNING.md`
- Debugging and visual debug: `docs/DEBUGGING.md`
- Networking and multiplayer bring-up: `docs/NETWORKING.md`
- Build matrix and platform notes: `docs/BUILD_PLATFORMS.md`
- Architecture: `docs/ARCHITECTURE.md`
- Extending guide: `docs/EXTENDING.md`

## Networking
For full server/client setup and validation steps, see `docs/NETWORKING.md`.

## Docs
- `docs/SETUP.md`
- `docs/RUNNING.md`
- `docs/DEBUGGING.md`
- `docs/NETWORKING.md`
- `docs/BUILD_PLATFORMS.md`
- `docs/ARCHITECTURE.md`
- `docs/EXTENDING.md`
- `docs/GENESIS_REFACTOR_PLAN.md`
