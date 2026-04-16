# Running VOXOV

## Desktop Runtime

Desktop:

```bash
./build/desktop/main/bin/voxov
```

Window mode and size:

```bash
./build/desktop/main/bin/voxov --windowed --width 1600 --height 900
./build/desktop/main/bin/voxov --fullscreen
```

Connect to a server:

```bash
./build/desktop/main/bin/voxov --connect 127.0.0.1 --port 7777 --devhud
```

Host in one process:

```bash
./build/desktop/main/bin/voxov --server
```

Standalone dedicated server:

```bash
./build/desktop/main/bin/voxov_server --port 7777
```

Compatibility headless server mode:

```bash
./build/desktop/main/bin/voxov --headless-server --port 7777
```

## Useful Runtime Flags

- `--physics jolt|avbd`
- `--devhud`
- `--noclip`
- `--splitscreen`
- `--debug-collision`
- `--debug-xray`
- `--debug-collision-only`
- `--debug-freeze`
- `--vehicle-sandbox`
- `--spherical-planet`
- `--flat-world`
- `--fullscreen`
- `--windowed`
- `--width <pixels>`
- `--height <pixels>`
- `--connect <host>`
- `--port <port>`
- `--server`
- `--headless-server`

Notes:

- Desktop now uses the OpenGL renderer by default.
- `--renderer gl` is still accepted as a compatibility alias, but it is no longer required.
- `voxov_server` is the preferred standalone authoritative server target.
- `voxov --headless-server` remains useful for compatibility and quick local bring-up, but it is no longer the only dedicated-server path.
- `voxov_server` now prints rate-limited `TEL ...` telemetry lines (about every 5 seconds) with loop, network, and server activity counters.

## Desktop Hotkeys

- `F11` toggle fullscreen
- `Esc` release mouse capture
- Right-click recapture mouse look
- `F1` toggle collision debug draw
- `F2` toggle xray debug draw
- `F3` toggle collision-only debug draw
- `F4` freeze/unfreeze current debug frame
- `F5` cycle reconcile mode

## Tests

Build tests first:

```bash
cmake -S . -B build/desktop/main -DVOXOV_BUILD_TESTS=ON
cmake --build build/desktop/main --parallel
```

Run:

```bash
ctest --test-dir build/desktop/main --output-on-failure
```

The network stress test binds loopback ports, so it may fail inside restrictive sandboxes even when the project is healthy.

## Asset Cooker

```bash
./build/desktop/main/bin/voxov_asset_cooker gltf assets/ship.glb build/desktop/main/ship.vasset
./build/desktop/main/bin/voxov_asset_cooker texture assets/albedo.ktx2 build/desktop/main/albedo.vtex
```

## Related Docs

- Setup/build details: `docs/SETUP.md`
- Platform support: `docs/BUILD_PLATFORMS.md`
- Networking details: `docs/NETWORKING.md`
- Release workflow: `docs/RELEASES.md`
