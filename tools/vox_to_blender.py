#!/usr/bin/env python3
"""Import a ship .vox into Blender for inspection.

Blender has no .vox support, so this parses the file directly and builds one
mesh per palette material, emitting only the faces that touch air - the same
hidden-face cull the chunk mesher does, which keeps the interior walkable
volume visible when you clip into the hull.

Run:
  blender --python tools/vox_to_blender.py -- [assets/models/ships/courier.vox]

The trailing argument is optional; it defaults to the courier.
"""

import struct
import sys
from pathlib import Path

import bpy

# Faces as (normal offset, four corner offsets) in voxel space.
FACES = [
    ((1, 0, 0), [(1, 0, 0), (1, 1, 0), (1, 1, 1), (1, 0, 1)]),
    ((-1, 0, 0), [(0, 0, 0), (0, 0, 1), (0, 1, 1), (0, 1, 0)]),
    ((0, 1, 0), [(0, 1, 0), (0, 1, 1), (1, 1, 1), (1, 1, 0)]),
    ((0, -1, 0), [(0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1)]),
    ((0, 0, 1), [(0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]),
    ((0, 0, -1), [(0, 0, 0), (0, 1, 0), (1, 1, 0), (1, 0, 0)]),
]


def read_vox(path):
    data = path.read_bytes()
    assert data[:4] == b"VOX ", f"{path} is not a .vox file"
    pos, voxels, palette = 8, {}, {}
    while pos < len(data):
        tag = data[pos:pos + 4]
        content_len, children_len = struct.unpack("<ii", data[pos + 4:pos + 12])
        body = data[pos + 12:pos + 12 + content_len]
        if tag == b"XYZI":
            count = struct.unpack("<i", body[:4])[0]
            for i in range(count):
                x, y, z, c = struct.unpack("<BBBB", body[4 + i * 4:8 + i * 4])
                voxels[(x, y, z)] = c
        elif tag == b"RGBA":
            for i in range(256):
                r, g, b, a = struct.unpack("<BBBB", body[i * 4:i * 4 + 4])
                if a:
                    palette[i + 1] = (r / 255, g / 255, b / 255)
        # MAIN carries its payload as children, so descend rather than skip.
        pos += 12 + content_len if tag == b"MAIN" else 12 + content_len + children_len
    return voxels, palette


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def make_material(name, rgb):
    material = bpy.data.materials.new(name)
    linear = tuple(srgb_to_linear(c) for c in rgb)
    material.diffuse_color = (*linear, 1.0)       # solid viewport shading
    material.use_nodes = True
    bsdf = material.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*linear, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.6
    return material


def build(voxels, palette, name):
    """One mesh per material, exposed faces only."""
    objects = []
    for index in sorted({c for c in voxels.values()}):
        verts, faces = [], []
        for (x, y, z), c in voxels.items():
            if c != index:
                continue
            for (nx, ny, nz), corners in FACES:
                if (x + nx, y + ny, z + nz) in voxels:
                    continue            # hidden: neighbour fills this face
                base = len(verts)
                verts += [(x + dx, y + dy, z + dz) for dx, dy, dz in corners]
                faces.append((base, base + 1, base + 2, base + 3))

        if not faces:
            continue
        mesh = bpy.data.meshes.new(f"{name}_mat{index}")
        mesh.from_pydata(verts, [], faces)
        mesh.validate()
        mesh.update()
        obj = bpy.data.objects.new(f"{name}_mat{index}", mesh)
        obj.data.materials.append(
            make_material(f"{name}_mat{index}", palette.get(index, (1, 0, 1))))
        bpy.context.scene.collection.objects.link(obj)
        objects.append(obj)
    return objects


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    root = Path(__file__).resolve().parent.parent
    path = Path(argv[0]) if argv else root / "assets/models/ships/courier.vox"
    if not path.is_absolute():
        path = root / path

    bpy.ops.wm.read_factory_settings(use_empty=True)
    voxels, palette = read_vox(path)
    objects = build(voxels, palette, path.stem)

    # Centre the ship on the origin so orbiting the view behaves.
    xs = [v[0] for v in voxels]
    ys = [v[1] for v in voxels]
    zs = [v[2] for v in voxels]
    offset = (-(min(xs) + max(xs)) / 2, -(min(ys) + max(ys)) / 2, -min(zs))
    for obj in objects:
        obj.location = offset

    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]

    # Solid shading with material colours, and frame the ship in every 3D view.
    # A --python script runs before the window manager builds the UI, so
    # bpy.context.screen is still None here; defer until the first redraw.
    def setup_views():
        screen = getattr(bpy.context, "screen", None)
        if screen is None:
            return 0.1          # UI not up yet, try again
        for area in screen.areas:
            if area.type != "VIEW_3D":
                continue
            for space in area.spaces:
                if space.type == "VIEW_3D":
                    space.shading.type = "SOLID"
                    space.shading.color_type = "MATERIAL"
                    space.clip_end = 1000.0
            region = next((r for r in area.regions if r.type == "WINDOW"), None)
            if region is not None:
                bpy.ops.view3d.view_selected({"area": area, "region": region})
        return None             # done, unregister

    if not bpy.app.background:
        bpy.app.timers.register(setup_views, first_interval=0.1)

    faces = sum(len(o.data.polygons) for o in objects)
    print(f"[vox] {path.name}: {len(voxels)} voxels, {len(objects)} meshes, "
          f"{faces} faces, size {max(xs) - min(xs) + 1} x {max(ys) - min(ys) + 1} "
          f"x {max(zs) - min(zs) + 1}")


main()
