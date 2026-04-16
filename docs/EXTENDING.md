# Extending VOXOV

## Add a renderer feature

1. Add shared data in `src/engine_render/render_types.hpp`.
2. Update `IRenderBackend` in `src/engine_render/render_backend.hpp` only if the feature needs backend API changes.
3. Implement in the desktop GL backend (`src/engine_render/gl_renderer.cpp`).
4. Keep the feature inside the GLES3/WebGL2-class capability budget used by the project.
5. Validate in the desktop runtime, then check Android/Web paths if the feature affects shared render contracts.

## Add a replicated component

1. Define wire types and packet headers in `src/engine_net_proto/net_types.hpp`.
2. Add helper builders or validators in `src/engine_net_proto/net_protocol_helpers.hpp` if the message needs shared protocol logic.
3. Add transport handling in `src/engine_net/net_server.cpp` and `src/engine_net/net_client.cpp`.
4. Keep server-side session/state ownership in `src/engine_server/server_session.*` or runtime code instead of pushing policy into protocol headers.
5. Pick channel:
   - reliable: `NetChannel::Reliable`
   - transient/unreliable: `NetChannel::Unreliable`
6. Add or extend serialization tests in `src/tests/test_main.cpp`.

## Add a new asset type

1. Add input/cooked metadata format in `src/tools/asset_cooker.cpp`.
2. Add runtime loader and registry entry (next module target: `engine_assets`).
3. Extend docs with source -> cooked workflow and expected runtime payload.

Current cooker modes:
- `voxov_asset_cooker gltf <input> <output>`
- `voxov_asset_cooker texture <input> <output>`
