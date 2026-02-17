# Running VOXOV

## Desktop Runtime

Vulkan:

```bash
./build/bin/voxov --renderer vulkan
```

OpenGL:

```bash
./build/bin/voxov --renderer gl
```

Window size and mode:

```bash
./build/bin/voxov --renderer vulkan --windowed --width 1600 --height 900
./build/bin/voxov --renderer vulkan --fullscreen
```

Runtime toggle:

- Press `F11` to toggle fullscreen/windowed.
- Press `Esc` to release mouse capture (so you can move/resize windows).
- Right-click in the game window to recapture mouse look.

Combined client + server in one process:

```bash
./build/bin/voxov --server
```

Headless dedicated server:

```bash
./build/bin/voxov --headless-server --port 7777
```

## Testing

```bash
ctest --test-dir build --output-on-failure
```

## Asset Cooker

```bash
./build/bin/voxov_asset_cooker gltf assets/ship.glb build/ship.vasset
./build/bin/voxov_asset_cooker texture assets/albedo.ktx2 build/albedo.vtex
```

## Related Docs

- Setup/build details: `docs/SETUP.md`
- Networking bring-up: `docs/NETWORKING.md`
- Debugging and tooling: `docs/DEBUGGING.md`
