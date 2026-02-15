# Extending VOXOV

## Add a renderer feature

1. Add shared data in `src/engine_render/render_types.hpp`.
2. Update `IRenderBackend` in `src/engine_render/render_backend.hpp` only if the feature needs backend API changes.
3. Implement in Vulkan backend (`src/engine_render/vulkan_renderer.cpp`).
4. Mirror behavior in GL backend (`src/engine_render/gl_renderer.cpp`) or provide a fallback path.
5. Validate with `--renderer vulkan` and `--renderer gl`.

## Add a replicated component

1. Define payload in `src/engine_net/net_common.hpp`.
2. Add message handling in `src/engine_net/net_server.cpp` and `src/engine_net/net_client.cpp`.
3. Pick channel:
   - reliable: `NetChannel::Reliable`
   - transient/unreliable: `NetChannel::Unreliable`
4. Add/extend serialization tests in `src/tests/test_main.cpp`.

## Add a new asset type

1. Add input/cooked metadata format in `src/tools/asset_cooker.cpp`.
2. Add runtime loader and registry entry (next module target: `engine_assets`).
3. Extend docs with source -> cooked workflow and expected runtime payload.

Current cooker modes:
- `voxov_asset_cooker gltf <input> <output>`
- `voxov_asset_cooker texture <input> <output>`
