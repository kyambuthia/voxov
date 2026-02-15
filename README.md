## VOXOV

VOXOV is a voxel exploration engine foundation in C++ with Vulkan and OpenGL backends.

## Build

```bash
cmake -S . -B build -DVOXOV_BUILD_TESTS=ON
cmake --build build -j
```

## Run

```bash
./build/bin/voxov --renderer vulkan
./build/bin/voxov --renderer gl
```

Server modes:

```bash
./build/bin/voxov --server
./build/bin/voxov --headless-server
```

Asset cooking:

```bash
./build/bin/voxov_asset_cooker gltf assets/ship.glb build/ship.vasset
./build/bin/voxov_asset_cooker texture assets/albedo.ktx2 build/albedo.vtex
```

## Docs

- `docs/GENESIS_REFACTOR_PLAN.md`
- `docs/ARCHITECTURE.md`
- `docs/BUILD_PLATFORMS.md`
- `docs/EXTENDING.md`
- `docs/SETUP.md`
