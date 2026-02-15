# Player Controller Notes

## Coordinate System
- Right-handed world.
- `+Y` is up.
- `+Z` is forward at yaw `0`.
- `+X` is right.
- Strafe basis uses `right = normalize(cross(up, forward))`.

## Input Axis Conventions
- Move axis:
  - `MoveX`: `D = +1`, `A = -1`
  - `MoveY`: `W = +1`, `S = -1`
- Look axis (desktop mouse delta per frame):
  - `LookX` controls yaw (`yaw += LookX * sensitivity`)
  - `LookY` controls pitch (`pitch -= LookY * sensitivity`, clamped)

## Desktop Mouse Delta
- `RMB` held enables look mode + pointer lock.
- Mouse delta is computed each frame from GLFW cursor position:
  - `mouse_dx = current_x - last_x`
  - `mouse_dy = current_y - last_y`
- If available, raw mouse motion is enabled while pointer lock is active.
- Input is sampled before camera/controller simulation each frame.
