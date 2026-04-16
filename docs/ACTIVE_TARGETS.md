# VOXOV Active Targets

This repository currently ships these active entrypoints:

- Desktop: `src/game/main.cpp` (`voxov`)
- Dedicated server: `src/game/server_main.cpp` (`voxov_server`)
- Android: `src/game/android_main.cpp` (`voxov_android`)
- Web preview: `src/game/web_main.cpp` (`voxov_web`)

Desktop is the first target booting through the shared `GameRuntime` seam. Android and Web still keep separate runtime loops.

The desktop client also still supports combined local client/server bring-up via `--server` and the older `--headless-server` compatibility mode, but the preferred dedicated-server entrypoint is `voxov_server`.

The repository also contains non-shipping platform scaffolds that are disabled by default:

- iOS backend scaffold: `src/platform/ios_platform.cpp`
- Console backend scaffold: `src/platform/console_platform.cpp`
- XR session scaffold: `src/engine_xr/xr_session.cpp`

The following source files are legacy/demo codepaths and are not built by default:

- `src/main.cpp`
- `src/game.cpp`
- `src/renderer/vulkan_app.cpp`
- `src/world/planet_world.cpp`
- `src/engine_assets/static_model.cpp`

If these legacy paths are revived, move them behind explicit CMake options and keep them isolated from the shipping runtime.
