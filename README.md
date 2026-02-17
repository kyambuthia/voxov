## VOXOV
Download platform builds from GitHub Releases: https://github.com/kyambuthia/voxov/releases

## Download Builds (0.0.14)
- Release page: https://github.com/kyambuthia/voxov/releases/tag/0.0.14
- Android APK: https://github.com/kyambuthia/voxov/releases/download/0.0.14/VOXOV-0.0.14-android-arm64-v8a.apk
- Windows executable (zip): https://github.com/kyambuthia/voxov/releases/download/0.0.14/VOXOV-0.0.14-windows-x86_64.zip
- Linux executable: https://github.com/kyambuthia/voxov/releases/download/0.0.14/VOXOV-0.0.14-linux-x86_64

Short requirements to run:
- Android: Android 8.0+ recommended, arm64 device. Enable installation from unknown sources, install APK, launch `VOXOV`.
- Windows: 64-bit Windows 10/11. Unzip and run `voxov.exe` from the extracted folder.
- Linux: 64-bit Linux with OpenGL drivers (`libGL`) and common X11/Wayland runtime libs. `chmod +x VOXOV-0.0.14-linux-x86_64` then run it.

## Status
`WIP` (work in progress). This project is actively evolving and APIs, runtime behavior, and platform support can change between commits.

Current notes:
- Vulkan path is under active stabilization (validation-clean sync and swapchain behavior still being improved).
- Some CPUs may require disabling aggressive Jolt SIMD options (e.g. AVX2/F16C/FMADD/LZCNT) to avoid illegal-instruction startup failures.

## Current Showcase
Gameplay tested on Android:

![VOXOV Android Gameplay GIF](docs/media/voxov_android_gameplay.gif)

Gameplay clip tested on Debian 13 PC - tag (GIF):

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
- AVBD physics integration (mobile + cross-platform plan): `docs/AVBD_INTEGRATION.md`
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
- `docs/AVBD_INTEGRATION.md`
- `docs/EXTENDING.md`
- `docs/GENESIS_REFACTOR_PLAN.md`
