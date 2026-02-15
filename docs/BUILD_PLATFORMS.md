# Build Targets

## Desktop (Windows/Linux/macOS)

```bash
cmake -S . -B build -DVOXOV_BUILD_TESTS=ON
cmake --build build -j
```

Run Vulkan backend:

```bash
./build/bin/voxov --renderer vulkan
```

Run OpenGL backend:

```bash
./build/bin/voxov --renderer gl
```

Run headless authoritative server:

```bash
./build/bin/voxov --headless-server
```

## Android (foundation)

Skeleton backend compiled: `src/platform/android_platform.cpp`.
Desktop build keeps it as a no-op stub unless `__ANDROID__` is defined.

Suggested next step:
- add `src/platform/android_platform.cpp`
- add `VOXOV_PLATFORM_ANDROID` compile option
- route Vulkan backend window surface creation through platform abstraction.

## Web (foundation)

Skeleton backend compiled: `src/platform/web_platform.cpp`.
Desktop build keeps it as a no-op stub unless `__EMSCRIPTEN__` is defined.

Suggested next step:
- add Emscripten toolchain target in CMake
- implement `web_platform.cpp` with emscripten main loop
- replace GL fixed-function draw path with GLSL ES 3.0 shaders for WebGL2 compatibility.
