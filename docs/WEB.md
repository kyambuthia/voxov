# Web Build (Emscripten)

## Status

Web support is an active preview target with:

- a playable local movement loop
- menu/devhud flow parity with desktop controls
- shared Web session-flow orchestration (`WebSessionFlow`) for host/join/leave state
- WebGL2 runtime bring-up
- optional JS transport hooks for host/join state exchange

The current preview is still JS-heavy. `src/game/web_main.cpp` still owns the movement loop and browser hook integration, while the Emscripten build compiles a focused subset of shared runtime helpers (`RuntimeSessionController`, `GuiMenu`, and `WebSessionFlow`). The next step is not "more JavaScript"; it is more shared C++ compiled to WASM.

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
2. Full gameplay/runtime parity with the desktop OpenGL-first runtime remains future work.

## WASM Integration Direction

The right integration direction for Web is:

1. Compile more shared code into WASM first.
   - Move the Web target toward `GameRuntime`, `engine_runtime`, `engine_gameplay`, `engine_world`, `engine_math`, and `engine_net_proto` instead of keeping equivalent logic in `web_main.cpp`.
   - Keep browser-specific concerns out of those libraries.
2. Keep JavaScript as a thin browser adapter.
   - JavaScript should own DOM bootstrapping, canvas setup, input event collection, persistence hooks, and browser-safe networking integration.
   - JavaScript should not own gameplay state, menu rules, movement simulation, or protocol definitions.
3. Introduce Web-specific runtime adapters instead of a separate runtime.
   - Mirror the desktop shape with `IRuntimePlatformAdapter` and `IRuntimeInputAdapter` implementations for Emscripten/browser input.
   - Feed browser events into `GameRuntime` rather than reimplementing session flow in the Web entrypoint.
4. Keep transport browser-appropriate.
   - Do not try to force ENet directly into the browser runtime model.
   - Keep `engine_net_proto` in WASM and expose a thin transport shim for WebRTC, WebTransport, or a WebSocket bridge.
   - That preserves one protocol definition across desktop, server, Android, and Web even if transport implementations differ.
5. Preserve the rendering budget.
   - Continue treating WebGL2 and GLES3-class limits as the content/rendering ceiling.
   - Prefer shared render-facing snapshots and scene assembly in C++, with only the unavoidable WebGL/browser bootstrap remaining platform-specific.

## Practical Next Steps

1. Add a Web runtime adapter pair and reuse `GameRuntime` from the Emscripten build.
2. Move menu/session control entirely into shared runtime code on Web.
3. Move local movement and simulation state out of `web_main.cpp` and into shared gameplay/runtime modules.
4. Keep the current JS hooks, but reduce them to transport and browser I/O only.
5. Once that is stable, replace ad-hoc local/remote browser state with protocol-driven state built from shared `engine_net_proto` types.
