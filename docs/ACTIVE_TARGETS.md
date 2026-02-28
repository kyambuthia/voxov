# VOXOV Active Targets

This repository currently ships these primary runtime entrypoints:

- Desktop: `src/game/main.cpp` (`voxov`)
- Android: `src/game/android_main.cpp` (`voxov_android`)
- Web: `src/game/web_main.cpp` (`voxov_web`)

The repository also compiles non-shipping platform scaffolds:

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
