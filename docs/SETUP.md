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

Desktop debug flags:

```bash
./build/bin/voxov --renderer vulkan --devhud
./build/bin/voxov --renderer vulkan --devhud --noclip
```

Authoritative server modes:

```bash
./build/bin/voxov --server
./build/bin/voxov --headless-server
```

LAN (two clients, same Wi-Fi):

```bash
# PC A
./build/bin/voxov --headless-server --port 7777

# PC B
./build/bin/voxov --renderer vulkan --connect <PC_A_LAN_IP> --port 7777 --devhud

# PC C
./build/bin/voxov --renderer gl --connect <PC_A_LAN_IP> --port 7777 --devhud
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
