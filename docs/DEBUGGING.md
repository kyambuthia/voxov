# Debugging Guide

## Controls (Desktop)

- Mouse controls third-person camera orbit (GTA-style while focused)
- `W/A/S/D` move
- `Space` jump
- `Shift` sprint
- `Q/E` camera distance
- `Esc` GUI menu (W/S or arrows + Enter)

## Runtime Debug Flags

General gameplay debug:

```bash
./build/bin/voxov --devhud
./build/bin/voxov --devhud --noclip
./build/bin/voxov --splitscreen
```

Collision visualization:

```bash
./build/bin/voxov --debug-collision
./build/bin/voxov --debug-xray
./build/bin/voxov --debug-collision-only
./build/bin/voxov --debug-freeze
```

## Debug Hotkeys

- `F1` toggle collision debug draw
- `F2` toggle xray mode (depth-disabled debug draw)
- `F3` toggle collision-only debug primitives
- `F4` freeze/unfreeze current debug frame

## Dear ImGui

- Dear ImGui is integrated in both renderer paths.
- OpenGL and Vulkan show an in-game `VOXOV Debug` panel with runtime metrics and debug hotkey reminders.
