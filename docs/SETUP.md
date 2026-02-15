# Setup and Build

## Prerequisites

- CMake 3.16+
- Vulkan SDK (with `glslc`)
- OpenGL development libraries
- C++23 compiler
- Git with submodules enabled

## Clone

```bash
git clone --recurse-submodules github.com/kyambuthia/voxov.git
cd voxov
```

## Configure and Build

```bash
cmake -S . -B build -DVOXOV_BUILD_TESTS=ON
cmake --build build -j
```

## Run

```bash
./build/bin/voxov --renderer vulkan
./build/bin/voxov --renderer gl
```

Authoritative server modes:

```bash
./build/bin/voxov --server
./build/bin/voxov --headless-server
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

Cook assets:

```bash
./build/bin/voxov_asset_cooker gltf assets/ship.glb build/ship.vasset
./build/bin/voxov_asset_cooker texture assets/albedo.ktx2 build/albedo.vtex
```
