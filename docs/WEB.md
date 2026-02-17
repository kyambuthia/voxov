# Web Build (Emscripten)

## Status

Web support is currently an MVP target with a minimal runtime loop and WebGL2 bring-up path.

## Prerequisites

1. Emscripten SDK installed and activated.
2. CMake and Ninja available.

## Configure

From repository root:

```bash
emcmake cmake -S . -B build-web -G Ninja
```

## Build

```bash
cmake --build build-web --parallel
```

Expected output:

- `build-web/voxov_web.js`
- `build-web/voxov_web.wasm`
- `build-web/voxov_web.html`

## Run locally

Option 1:

```bash
emrun --no_browser --port 8080 build-web/voxov_web.html
```

Option 2:

```bash
python3 -m http.server 8080 --directory build-web
```

Then open:

- `http://localhost:8080/voxov_web.html`

## Notes

1. This MVP validates platform/bootstrap/render loop on web.
2. Full gameplay/render parity with desktop/mobile is future work.
