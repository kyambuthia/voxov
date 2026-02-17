## VOXOV
Download platform builds from GitHub Releases: https://github.com/kyambuthia/voxov/releases

## Status
`WIP` (work in progress). This project is actively evolving and APIs, runtime behavior, and platform support can change between commits.

Current notes:
- Vulkan path is under active stabilization (validation-clean sync and swapchain behavior still being improved).
- Some CPUs may require disabling aggressive Jolt SIMD options (e.g. AVX2/F16C/FMADD/LZCNT) to avoid illegal-instruction startup failures.

## Current Showcase
Latest gameplay clip (GIF from latest screencast):

![VOXOV Gameplay GIF](docs/media/voxov_state.gif)

Latest screenshots:

| Screenshot 1 | Screenshot 2 |
| --- | --- |
| ![VOXOV Screenshot 1](docs/media/voxov_state_01.png) | ![VOXOV Screenshot 2](docs/media/voxov_state_02.png) |

## Documentation

- Setup and build: `docs/SETUP.md`
- Running and testing: `docs/RUNNING.md`
- Debugging and visual debug: `docs/DEBUGGING.md`
- Networking and multiplayer bring-up: `docs/NETWORKING.md`
- Release process and binaries: `docs/RELEASES.md`
- Build matrix and platform notes: `docs/BUILD_PLATFORMS.md`
- Android build/setup: `docs/ANDROID.md`
- Architecture: `docs/ARCHITECTURE.md`
- Infinite voxel world design (renderer + networking + multiplayer): `docs/VOXEL_WORLD_DESIGN.md`
- Extending guide: `docs/EXTENDING.md`

## Networking
For full server/client setup and validation steps, see `docs/NETWORKING.md`.

## Docs
- `docs/SETUP.md`
- `docs/RUNNING.md`
- `docs/DEBUGGING.md`
- `docs/NETWORKING.md`
- `docs/RELEASES.md`
- `docs/BUILD_PLATFORMS.md`
- `docs/ANDROID.md`
- `docs/ARCHITECTURE.md`
- `docs/VOXEL_WORLD_DESIGN.md`
- `docs/EXTENDING.md`
- `docs/GENESIS_REFACTOR_PLAN.md`
