# Web Build (Emscripten)

## Status

Web support is an active preview target with:

- a playable local movement loop
- menu/devhud flow parity with desktop controls
- WebGL2 runtime bring-up
- optional JS transport hooks for host/join state exchange

## Prerequisites

1. Emscripten SDK installed and activated.
2. CMake and Ninja available.

## Configure

From repository root:

```bash
EM_CACHE=./build/web/cache emcmake cmake -S . -B build/web/main -G Ninja
```

## Build

```bash
EM_CACHE=./build/web/cache cmake --build build/web/main --parallel
```

Expected output:

- `build/web/main/bin/voxov_web.js`
- `build/web/main/bin/voxov_web.wasm`
- `build/web/main/bin/voxov_web.html`

## Run locally

Option 1:

```bash
emrun --no_browser --port 8080 build/web/main/bin/voxov_web.html
```

Option 2:

```bash
python3 -m http.server 8080 --directory build/web/main/bin
```

Then open:

- `http://localhost:8080/voxov_web.html`

## Notes

1. Attach optional transport hooks in JS:
   - `Module.__voxovNetHost()`
   - `Module.__voxovNetJoin()`
   - `Module.__voxovNetSendLocal(x,y,z,yaw)`
   - `Module.__voxovNetRemoteCount()`
2. Full renderer parity with desktop Vulkan/OpenGL remains future work.
