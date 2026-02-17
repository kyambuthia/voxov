# Running VOXOV

## Desktop Runtime

Vulkan:

```bash
./build/desktop/main/bin/voxov --renderer vulkan
```

OpenGL:

```bash
./build/desktop/main/bin/voxov --renderer gl
```

Window size and mode:

```bash
./build/desktop/main/bin/voxov --renderer vulkan --windowed --width 1600 --height 900
./build/desktop/main/bin/voxov --renderer vulkan --fullscreen
```

Runtime toggle:

- Press `F11` to toggle fullscreen/windowed.
- Press `Esc` to release mouse capture (so you can move/resize windows).
- Right-click in the game window to recapture mouse look.

Combined client + server in one process:

```bash
./build/desktop/main/bin/voxov --server
```

Headless dedicated server:

```bash
./build/desktop/main/bin/voxov --headless-server --port 7777
```

## Testing

```bash
ctest --test-dir build/desktop/main --output-on-failure
```

## Asset Cooker

```bash
./build/desktop/main/bin/voxov_asset_cooker gltf assets/ship.glb build/desktop/main/ship.vasset
./build/desktop/main/bin/voxov_asset_cooker texture assets/albedo.ktx2 build/desktop/main/albedo.vtex
```

## Related Docs

- Setup/build details: `docs/SETUP.md`
- Networking bring-up: `docs/NETWORKING.md`
- Debugging and tooling: `docs/DEBUGGING.md`
