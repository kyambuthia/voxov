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

- `voxov` is the engine demo target.

## Run

From the build directory:

```bash
./bin/voxov
```

Run server + client:

```bash
./bin/voxov --server
./bin/voxov --connect 127.0.0.1
```

## Troubleshooting

- If the window fails to open or the app exits immediately, ensure you have a working display
  and Vulkan runtime. On Linux, verify `DISPLAY` (X11) or `WAYLAND_DISPLAY` is set.
- If `glslc` is missing, install the Vulkan SDK and ensure it is in `PATH`.
- If submodules are missing, re-run `git submodule update --init --recursive`.
