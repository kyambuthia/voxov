#!/usr/bin/env python3
"""Render the generated ship .vox files to a review sheet.

A check on the generator, not an art pipeline: it draws an isometric view plus
the three orthographic elevations so the silhouette can be compared directly
against the reference art in assets/references/spaceships without opening
Blender or MagicaVoxel. Writes <name>_preview.png next to the model.

Usage: python3.13 tools/preview_ship_voxels.py
"""

from __future__ import annotations

import struct
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
MODEL_DIR = ROOT / "assets" / "models" / "ships"

SCALE = 12          # pixels per voxel edge
MARGIN = 16
BACKGROUND = (255, 255, 255)
TOP, LEFT, RIGHT = 1.0, 0.74, 0.55   # isometric face shading


def read_vox(path: Path):
    data = path.read_bytes()
    assert data[:4] == b"VOX ", "not a .vox file"
    pos, size, voxels, palette = 8, None, [], {}
    while pos < len(data):
        tag = data[pos:pos + 4]
        content_len, children_len = struct.unpack("<ii", data[pos + 4:pos + 12])
        body = data[pos + 12:pos + 12 + content_len]
        if tag == b"SIZE":
            size = struct.unpack("<iii", body)
        elif tag == b"XYZI":
            count = struct.unpack("<i", body[:4])[0]
            for i in range(count):
                x, y, z, c = struct.unpack("<BBBB", body[4 + i * 4:8 + i * 4])
                voxels.append((x, y, z, c))
        elif tag == b"RGBA":
            for i in range(256):
                r, g, b, a = struct.unpack("<BBBB", body[i * 4:i * 4 + 4])
                if a:
                    palette[i + 1] = (r, g, b)
        # MAIN carries its payload as children, so descend rather than skip.
        pos += 12 + content_len if tag == b"MAIN" else 12 + content_len + children_len
    return size, voxels, palette


def shade(rgb, factor):
    return tuple(max(0, min(255, int(c * factor))) for c in rgb)


def render_iso(size, voxels, palette) -> Image.Image:
    """vox axes: x right, y depth (ship length), z up."""
    sx, sy, sz = size
    half, quarter = SCALE, SCALE // 2

    def project(x, y, z):
        return (x - y) * half, (x + y) * quarter - z * SCALE

    corners = [project(x, y, z)
               for x in (0, sx) for y in (0, sy) for z in (0, sz)]
    min_u = min(c[0] for c in corners)
    min_v = min(c[1] for c in corners)
    width = max(c[0] for c in corners) - min_u + 2 * MARGIN
    height = max(c[1] for c in corners) - min_v + 2 * MARGIN

    image = Image.new("RGB", (int(width), int(height)), BACKGROUND)
    draw = ImageDraw.Draw(image)

    # Painter's algorithm: far voxels first. Two voxels land on the same screen
    # pixel when they differ by (1, 1, 1), so x+y+z is the depth axis here.
    for x, y, z, c in sorted(voxels, key=lambda v: v[0] + v[1] + v[2]):
        rgb = palette.get(c, (255, 0, 255))
        u, v = project(x, y, z)
        u, v = u - min_u + MARGIN, v - min_v + MARGIN
        draw.polygon([(u, v), (u + half, v + quarter),
                      (u, v + SCALE), (u - half, v + quarter)], fill=shade(rgb, TOP))
        draw.polygon([(u - half, v + quarter), (u, v + SCALE),
                      (u, v + 2 * SCALE), (u - half, v + SCALE + quarter)],
                     fill=shade(rgb, LEFT))
        draw.polygon([(u + half, v + quarter), (u, v + SCALE),
                      (u, v + 2 * SCALE), (u + half, v + SCALE + quarter)],
                     fill=shade(rgb, RIGHT))
    return image


def render_ortho(size, voxels, palette, view: str) -> Image.Image:
    """Flat elevation. Keeps the voxel nearest the camera at each screen cell.

    vox axes are x right, y ship length, z up, so:
      side  - look along -x, screen is (length, height), nose to the left
      top   - look down -z, screen is (length, width)
      front - look along -y from the nose, screen is (width, height)
    """
    sx, sy, sz = size
    if view == "side":
        cols, rows, key, depth = sy, sz, lambda v: (v[1], v[2]), lambda v: v[0]
    elif view == "top":
        cols, rows, key, depth = sy, sx, lambda v: (v[1], v[0]), lambda v: v[2]
    else:  # front
        cols, rows, key, depth = sx, sz, lambda v: (v[0], v[2]), lambda v: v[1]

    nearest: dict[tuple[int, int], tuple[int, int]] = {}
    for voxel in voxels:
        cell, d = key(voxel), depth(voxel)
        if cell not in nearest or d > nearest[cell][0]:
            nearest[cell] = (d, voxel[3])

    image = Image.new("RGB", (cols * SCALE + 2 * MARGIN, rows * SCALE + 2 * MARGIN),
                      BACKGROUND)
    draw = ImageDraw.Draw(image)
    for (col, row), (_, c) in nearest.items():
        rgb = palette.get(c, (255, 0, 255))
        # Nose to the left for side and top; height grows upward on screen.
        u = MARGIN + (cols - 1 - col) * SCALE if view != "front" else MARGIN + col * SCALE
        v = MARGIN + (rows - 1 - row) * SCALE
        draw.rectangle([u, v, u + SCALE - 1, v + SCALE - 1], fill=rgb)
    return image


def sheet(images: list[Image.Image]) -> Image.Image:
    """Stack the views vertically, left aligned, on one background."""
    gap = MARGIN
    width = max(i.width for i in images)
    height = sum(i.height for i in images) + gap * (len(images) - 1)
    out = Image.new("RGB", (width, height), BACKGROUND)
    y = 0
    for image in images:
        out.paste(image, (0, y))
        y += image.height + gap
    return out


def main() -> int:
    for model in sorted(MODEL_DIR.glob("*.vox")):
        size, voxels, palette = read_vox(model)
        views = [render_iso(size, voxels, palette)]
        views += [render_ortho(size, voxels, palette, v)
                  for v in ("side", "top", "front")]
        out = model.with_name(f"{model.stem}_preview.png")
        sheet(views).save(out)
        print(f"{out.name}: {size[0]} x {size[2]} x {size[1]} (w x h x l)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
