# Debugging Guide

Following GEA §10, the engine provides in-game debugging tools: runtime metrics overlay,
collision visualization, and an integrated Dear ImGui debug panel.

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
./build/desktop/main/bin/voxov --devhud
./build/desktop/main/bin/voxov --devhud --noclip
./build/desktop/main/bin/voxov --splitscreen
```

Collision visualization:

```bash
./build/desktop/main/bin/voxov --debug-collision
./build/desktop/main/bin/voxov --debug-xray
./build/desktop/main/bin/voxov --debug-collision-only
./build/desktop/main/bin/voxov --debug-freeze
```

## Debug Hotkeys

- `F1` toggle collision debug draw
- `F2` toggle xray mode (depth-disabled debug draw)
- `F3` toggle collision-only debug primitives
- `F4` freeze/unfreeze current debug frame

## Dear ImGui

- Dear ImGui is integrated in the shared desktop renderer.
- The desktop `VOXOV Debug` panel shows runtime metrics and debug hotkey reminders.
