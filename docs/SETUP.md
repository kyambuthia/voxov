# Setup and Build

## Prerequisites

- CMake 3.16+
- Vulkan SDK (with `glslc`)
- A C++23 compiler
- Git with submodules enabled

## Clone

```bash
git clone --recurse-submodules github.com/kyambuthia/voxov.git
cd voxov
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

## Configure and Build

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

## Build Targets

- `game` is the new demo target.
- `voxov` is the legacy target.

## Run

From the build directory:

```bash
./bin/game
```

Run server + client:

```bash
./bin/game --server
./bin/game --connect 127.0.0.1
```

## Troubleshooting

- If `glslc` is missing, install the Vulkan SDK and ensure it is in `PATH`.
- If submodules are missing, re-run `git submodule update --init --recursive`.
