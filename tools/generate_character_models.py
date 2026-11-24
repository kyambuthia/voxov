import math
import os
from pathlib import Path

import bmesh
import bpy
from mathutils import Euler, Vector

OUT_DIR = Path(__file__).resolve().parent.parent / "assets" / "models" / "player"
OUT_DIR.mkdir(parents=True, exist_ok=True)


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for col in (bpy.data.meshes, bpy.data.armatures, bpy.data.actions, bpy.data.materials):
        for item in list(col):
            if item.users == 0:
                col.remove(item)


def add_box(bm, color_layer, center, size, bone_name, group_map, color, bevel=0.0):
    mat = bpy.data.meshes.new("_tmp")
    b = bmesh.new()
    bmesh.ops.create_cube(b, size=1.0)
    for v in b.verts:
        v.co.x = v.co.x * size[0] + center[0]
        v.co.y = v.co.y * size[1] + center[1]
        v.co.z = v.co.z * size[2] + center[2]
    if bevel > 0.0:
        bmesh.ops.bevel(b, geom=list(b.edges), offset=bevel, segments=1, affect="EDGES")
    b.to_mesh(mat)
    b.free()

    # Copy into main bmesh with vertex group & color
    deform_layer = bm.verts.layers.deform.verify()
    vg_idx = group_map[bone_name]
    vert_map = {}
    for mv in mat.vertices:
        nv = bm.verts.new(mv.co)
        nv[deform_layer][vg_idx] = 1.0
        vert_map[mv.index] = nv
    rgba = (color[0], color[1], color[2], 1.0)
    for poly in mat.polygons:
        f = bm.faces.new([vert_map[vi] for vi in poly.vertices])
        for loop in f.loops:
            loop[color_layer] = rgba
    bpy.data.meshes.remove(mat)


def create_armature(name):
    arm_data = bpy.data.armatures.new(name + "_Armature")
    arm_obj = bpy.data.objects.new(name + "_Rig", arm_data)
    bpy.context.collection.objects.link(arm_obj)
    bpy.context.view_layer.objects.active = arm_obj
    bpy.ops.object.mode_set(mode="EDIT")

    bones = arm_data.edit_bones
    # Blender coordinates: X=right/left, Y=back/front (-Y is forward), Z=up
    specs = [
        ("Pelvis", None, (0, 0, 0.88), (0, 0, 1.02)),
        ("Spine", "Pelvis", (0, 0, 1.02), (0, 0, 1.24)),
        ("Chest", "Spine", (0, 0, 1.24), (0, 0, 1.48)),
        ("Neck", "Chest", (0, 0, 1.48), (0, 0, 1.56)),
        ("Head", "Neck", (0, 0, 1.56), (0, 0, 1.84)),
        ("UpperArm.L", "Chest", (0.25, 0, 1.44), (0.28, 0, 1.18)),
        ("Forearm.L", "UpperArm.L", (0.28, 0, 1.18), (0.29, 0, 0.94)),
        ("Hand.L", "Forearm.L", (0.29, 0, 0.94), (0.29, 0, 0.82)),
        ("UpperArm.R", "Chest", (-0.25, 0, 1.44), (-0.28, 0, 1.18)),
        ("Forearm.R", "UpperArm.R", (-0.28, 0, 1.18), (-0.29, 0, 0.94)),
        ("Hand.R", "Forearm.R", (-0.29, 0, 0.94), (-0.29, 0, 0.82)),
        ("Thigh.L", "Pelvis", (0.12, 0, 0.88), (0.12, 0, 0.48)),
        ("Shin.L", "Thigh.L", (0.12, 0, 0.48), (0.12, 0, 0.10)),
        ("Foot.L", "Shin.L", (0.12, 0, 0.10), (0.12, -0.14, 0.04)),
        ("Thigh.R", "Pelvis", (-0.12, 0, 0.88), (-0.12, 0, 0.48)),
        ("Shin.R", "Thigh.R", (-0.12, 0, 0.48), (-0.12, 0, 0.10)),
        ("Foot.R", "Shin.R", (-0.12, 0, 0.10), (-0.12, -0.14, 0.04)),
    ]
    for bname, parent, head, tail in specs:
        eb = bones.new(bname)
        eb.head = Vector(head)
        eb.tail = Vector(tail)
        if parent:
            eb.parent = bones[parent]

    bpy.ops.object.mode_set(mode="OBJECT")
    return arm_obj, [s[0] for s in specs]


def build_character_mesh(name, arm_obj, bone_names, palette, variant):
    mesh = bpy.data.meshes.new(name + "_Mesh")
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    obj.parent = arm_obj

    group_map = {}
    for bname in bone_names:
        vg = obj.vertex_groups.new(name=bname)
        group_map[bname] = vg.index

    bm = bmesh.new()
    color_layer = bm.loops.layers.color.new("COLOR_0")

    c_main = palette["main"]
    c_sec = palette["secondary"]
    c_pants = palette["pants"]
    c_boots = palette["boots"]
    c_accent = palette["accent"]
    c_skin = palette["skin"]
    c_visor = palette["visor"]

    bev = 0.012 if variant in ("trooper", "medic") else 0.008

    # Torso & Pelvis
    add_box(bm, color_layer, (0, 0, 0.93), (0.34, 0.22, 0.14), "Pelvis", group_map, c_pants, bev)
    add_box(bm, color_layer, (0, 0, 1.01), (0.36, 0.24, 0.06), "Pelvis", group_map, c_boots, 0.004)
    add_box(bm, color_layer, (0, -0.125, 1.01), (0.08, 0.02, 0.05), "Pelvis", group_map, c_accent, 0.0)
    add_box(bm, color_layer, (0, 0, 1.14), (0.35, 0.23, 0.24), "Spine", group_map, c_main, bev)
    add_box(bm, color_layer, (0, 0, 1.37), (0.40, 0.26, 0.24), "Chest", group_map, c_main, bev)

    if variant == "trooper":
        add_box(bm, color_layer, (0, -0.14, 1.37), (0.30, 0.04, 0.16), "Chest", group_map, c_sec, 0.008)
        add_box(bm, color_layer, (0, 0.16, 1.36), (0.26, 0.11, 0.24), "Chest", group_map, c_sec, 0.01)
        add_box(bm, color_layer, (0, 0.21, 1.44), (0.18, 0.03, 0.04), "Chest", group_map, c_accent, 0.0)
    elif variant == "ranger":
        add_box(bm, color_layer, (0, 0, 1.47), (0.32, 0.24, 0.07), "Chest", group_map, c_accent, 0.006)
        add_box(bm, color_layer, (0, 0.16, 1.30), (0.24, 0.12, 0.20), "Chest", group_map, c_sec, 0.01)
    elif variant == "bot":
        add_box(bm, color_layer, (0, -0.14, 1.36), (0.24, 0.03, 0.14), "Chest", group_map, c_visor, 0.004)
        add_box(bm, color_layer, (0, 0.15, 1.36), (0.22, 0.10, 0.22), "Chest", group_map, c_sec, 0.006)
    elif variant == "medic":
        add_box(bm, color_layer, (0, -0.14, 1.37), (0.14, 0.03, 0.05), "Chest", group_map, c_accent, 0.0)
        add_box(bm, color_layer, (0, -0.14, 1.37), (0.05, 0.03, 0.14), "Chest", group_map, c_accent, 0.0)
        add_box(bm, color_layer, (0, 0.16, 1.35), (0.26, 0.12, 0.24), "Chest", group_map, c_accent, 0.01)

    # Neck + Head
    add_box(bm, color_layer, (0, 0, 1.52), (0.12, 0.12, 0.10), "Neck", group_map, c_skin, 0.0)
    if variant == "bot":
        add_box(bm, color_layer, (0, 0, 1.70), (0.28, 0.24, 0.24), "Head", group_map, c_main, 0.012)
        add_box(bm, color_layer, (0, -0.125, 1.71), (0.22, 0.03, 0.09), "Head", group_map, c_visor, 0.004)
        add_box(bm, color_layer, (-0.06, -0.135, 1.71), (0.045, 0.02, 0.045), "Head", group_map, c_accent, 0.0)
        add_box(bm, color_layer, (0.06, -0.135, 1.71), (0.045, 0.02, 0.045), "Head", group_map, c_accent, 0.0)
        add_box(bm, color_layer, (0.11, 0, 1.87), (0.025, 0.025, 0.12), "Head", group_map, c_accent, 0.0)
    else:
        add_box(bm, color_layer, (0, 0, 1.70), (0.26, 0.26, 0.26), "Head", group_map, c_skin, 0.012)
        add_box(bm, color_layer, (0, 0.01, 1.79), (0.28, 0.28, 0.11), "Head", group_map, c_sec, 0.012)
        if variant == "ranger":
            add_box(bm, color_layer, (0, -0.15, 1.75), (0.27, 0.08, 0.03), "Head", group_map, c_sec, 0.004)
        else:
            add_box(bm, color_layer, (0, -0.13, 1.70), (0.22, 0.03, 0.08), "Head", group_map, c_visor, 0.004)
        add_box(bm, color_layer, (-0.055, -0.138, 1.70), (0.035, 0.02, 0.03), "Head", group_map, c_accent, 0.0)
        add_box(bm, color_layer, (0.055, -0.138, 1.70), (0.035, 0.02, 0.03), "Head", group_map, c_accent, 0.0)

    # Arms (Left & Right)
    for side, sx in (("L", 1.0), ("R", -1.0)):
        add_box(bm, color_layer, (sx * 0.26, 0, 1.43), (0.14, 0.15, 0.10), f"UpperArm.{side}", group_map, c_sec, 0.01)
        add_box(bm, color_layer, (sx * 0.27, 0, 1.31), (0.11, 0.12, 0.22), f"UpperArm.{side}", group_map, c_main, bev)
        add_box(bm, color_layer, (sx * 0.28, 0, 1.06), (0.10, 0.11, 0.22), f"Forearm.{side}", group_map, c_sec, bev)
        add_box(bm, color_layer, (sx * 0.29, -0.01, 0.89), (0.09, 0.10, 0.11), f"Hand.{side}", group_map, c_accent, 0.006)

    # Legs (Left & Right)
    for side, sx in (("L", 1.0), ("R", -1.0)):
        add_box(bm, color_layer, (sx * 0.12, 0, 0.68), (0.14, 0.15, 0.36), f"Thigh.{side}", group_map, c_pants, bev)
        add_box(bm, color_layer, (sx * 0.12, 0, 0.29), (0.125, 0.135, 0.36), f"Shin.{side}", group_map, c_pants, bev)
        add_box(bm, color_layer, (sx * 0.12, -0.04, 0.06), (0.14, 0.23, 0.11), f"Foot.{side}", group_map, c_boots, 0.008)

    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()

    mat = bpy.data.materials.new(name + "_Mat")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Roughness"].default_value = 0.6
        vcol = mat.node_tree.nodes.new("ShaderNodeVertexColor")
        vcol.layer_name = "COLOR_0"
        mat.node_tree.links.new(vcol.outputs["Color"], bsdf.inputs["Base Color"])
    obj.data.materials.append(mat)

    mod = obj.modifiers.new(name="Armature", type="ARMATURE")
    mod.object = arm_obj
    return obj


def set_bone_rot(arm_obj, bone_name, rx_deg=0.0, ry_deg=0.0, rz_deg=0.0):
    pb = arm_obj.pose.bones[bone_name]
    pb.rotation_mode = "XYZ"
    pb.rotation_euler = Euler((math.radians(rx_deg), math.radians(ry_deg), math.radians(rz_deg)), "XYZ")


def set_bone_loc(arm_obj, bone_name, lx=0.0, ly=0.0, lz=0.0):
    pb = arm_obj.pose.bones[bone_name]
    pb.location = Vector((lx, ly, lz))


def key_all(arm_obj, frame):
    for pb in arm_obj.pose.bones:
        pb.keyframe_insert(data_path="rotation_euler", frame=frame)
        pb.keyframe_insert(data_path="location", frame=frame)


def reset_pose(arm_obj):
    for pb in arm_obj.pose.bones:
        pb.rotation_mode = "XYZ"
        pb.rotation_euler = Euler((0, 0, 0), "XYZ")
        pb.location = Vector((0, 0, 0))


def build_clip(arm_obj, clip_name, frames, pose_fn):
    act = bpy.data.actions.new(clip_name)
    arm_obj.animation_data_create()
    arm_obj.animation_data.action = act
    for f, t in frames:
        reset_pose(arm_obj)
        pose_fn(t)
        key_all(arm_obj, f)
    track = arm_obj.animation_data.nla_tracks.new()
    track.name = clip_name
    strip = track.strips.new(clip_name, 1, act)
    strip.name = clip_name
    arm_obj.animation_data.action = None


def create_animations(arm_obj):
    # Note: In local bone space, X rotation pitches forward/back, Z twists
    def pose_idle(t):
        s = math.sin(t * 2.0 * math.pi)
        set_bone_loc(arm_obj, "Pelvis", lz=0.015 * s)
        set_bone_rot(arm_obj, "Chest", rx_deg=2.0 * s)
        set_bone_rot(arm_obj, "Head", rx_deg=-2.0 * s)
        set_bone_rot(arm_obj, "UpperArm.L", rx_deg=3.0 * s, ry_deg=-4.0)
        set_bone_rot(arm_obj, "UpperArm.R", rx_deg=-3.0 * s, ry_deg=4.0)
        set_bone_rot(arm_obj, "Forearm.L", rx_deg=-8.0)
        set_bone_rot(arm_obj, "Forearm.R", rx_deg=-8.0)

    def pose_locomotion(t, amp, lean, bob):
        s = math.sin(t * 2.0 * math.pi)
        c = math.cos(t * 2.0 * math.pi)
        s2 = abs(s)
        set_bone_loc(arm_obj, "Pelvis", lz=-bob * s2)
        set_bone_rot(arm_obj, "Chest", rx_deg=lean, rz_deg=amp * 0.2 * s)
        set_bone_rot(arm_obj, "Head", rx_deg=-lean * 0.6)
        set_bone_rot(arm_obj, "Thigh.L", rx_deg=amp * s)
        set_bone_rot(arm_obj, "Shin.L", rx_deg=max(0.0, -amp * 0.9 * c))
        set_bone_rot(arm_obj, "Thigh.R", rx_deg=-amp * s)
        set_bone_rot(arm_obj, "Shin.R", rx_deg=max(0.0, amp * 0.9 * c))
        set_bone_rot(arm_obj, "UpperArm.L", rx_deg=-amp * 0.85 * s, ry_deg=-4.0)
        set_bone_rot(arm_obj, "Forearm.L", rx_deg=-22.0 - max(0.0, -amp * 0.4 * s))
        set_bone_rot(arm_obj, "UpperArm.R", rx_deg=amp * 0.85 * s, ry_deg=4.0)
        set_bone_rot(arm_obj, "Forearm.R", rx_deg=-22.0 - max(0.0, amp * 0.4 * s))

    def pose_jump(t):
        set_bone_loc(arm_obj, "Pelvis", lz=0.05)
        set_bone_rot(arm_obj, "Thigh.L", rx_deg=-28.0)
        set_bone_rot(arm_obj, "Shin.L", rx_deg=45.0)
        set_bone_rot(arm_obj, "Thigh.R", rx_deg=12.0)
        set_bone_rot(arm_obj, "Shin.R", rx_deg=20.0)
        set_bone_rot(arm_obj, "UpperArm.L", rx_deg=-40.0, ry_deg=-15.0)
        set_bone_rot(arm_obj, "UpperArm.R", rx_deg=-40.0, ry_deg=15.0)

    def pose_aim(t, recoil=0.0):
        k = recoil * math.sin(t * math.pi)
        set_bone_rot(arm_obj, "Chest", rz_deg=14.0, rx_deg=4.0 - k * 8.0)
        set_bone_rot(arm_obj, "Head", rz_deg=-12.0)
        set_bone_rot(arm_obj, "UpperArm.R", rx_deg=-86.0 - k * 14.0, ry_deg=8.0)
        set_bone_rot(arm_obj, "Forearm.R", rx_deg=-10.0)
        set_bone_rot(arm_obj, "UpperArm.L", rx_deg=-72.0 - k * 10.0, ry_deg=-26.0)
        set_bone_rot(arm_obj, "Forearm.L", rx_deg=-32.0)

    def pose_driving(t):
        s = math.sin(t * 2.0 * math.pi)
        set_bone_loc(arm_obj, "Pelvis", lz=-0.38)
        set_bone_rot(arm_obj, "Chest", rx_deg=8.0)
        set_bone_rot(arm_obj, "Thigh.L", rx_deg=-86.0)
        set_bone_rot(arm_obj, "Shin.L", rx_deg=82.0)
        set_bone_rot(arm_obj, "Thigh.R", rx_deg=-86.0)
        set_bone_rot(arm_obj, "Shin.R", rx_deg=82.0)
        set_bone_rot(arm_obj, "UpperArm.L", rx_deg=-56.0, ry_deg=-10.0, rz_deg=4.0 * s)
        set_bone_rot(arm_obj, "Forearm.L", rx_deg=-28.0)
        set_bone_rot(arm_obj, "UpperArm.R", rx_deg=-56.0, ry_deg=10.0, rz_deg=4.0 * s)
        set_bone_rot(arm_obj, "Forearm.R", rx_deg=-28.0)

    def pose_sitting(t):
        set_bone_loc(arm_obj, "Pelvis", lz=-0.38)
        set_bone_rot(arm_obj, "Thigh.L", rx_deg=-88.0)
        set_bone_rot(arm_obj, "Shin.L", rx_deg=88.0)
        set_bone_rot(arm_obj, "Thigh.R", rx_deg=-88.0)
        set_bone_rot(arm_obj, "Shin.R", rx_deg=88.0)
        set_bone_rot(arm_obj, "UpperArm.L", rx_deg=-25.0)
        set_bone_rot(arm_obj, "Forearm.L", rx_deg=-35.0)
        set_bone_rot(arm_obj, "UpperArm.R", rx_deg=-25.0)
        set_bone_rot(arm_obj, "Forearm.R", rx_deg=-35.0)

    loop_9 = [(1 + i * 3, i / 8.0) for i in range(9)]
    build_clip(arm_obj, "idle", loop_9, pose_idle)
    build_clip(arm_obj, "walk", loop_9, lambda t: pose_locomotion(t, 28.0, 5.0, 0.03))
    build_clip(arm_obj, "run", loop_9, lambda t: pose_locomotion(t, 44.0, 12.0, 0.06))
    build_clip(arm_obj, "sprint", loop_9, lambda t: pose_locomotion(t, 56.0, 18.0, 0.08))
    build_clip(arm_obj, "jump_idle", [(1, 0.0), (10, 1.0)], pose_jump)
    build_clip(arm_obj, "jump_running", [(1, 0.0), (10, 1.0)], pose_jump)
    build_clip(arm_obj, "falling", [(1, 0.0), (10, 1.0)], pose_jump)
    build_clip(arm_obj, "drop_idle", [(1, 0.0), (10, 1.0)], pose_idle)
    build_clip(arm_obj, "rotate_left", loop_9, lambda t: pose_locomotion(t, 16.0, 2.0, 0.02))
    build_clip(arm_obj, "rotate_right", loop_9, lambda t: pose_locomotion(t, 16.0, 2.0, 0.02))
    build_clip(arm_obj, "stop", [(1, 0.0), (8, 1.0)], pose_idle)
    build_clip(arm_obj, "aim_idle", [(1, 0.0), (10, 1.0)], lambda t: pose_aim(t, 0.0))
    build_clip(arm_obj, "shoot", [(1, 0.0), (4, 0.5), (9, 1.0)], lambda t: pose_aim(t, 1.0))
    build_clip(arm_obj, "driving", loop_9, pose_driving)
    build_clip(arm_obj, "sitting", [(1, 0.0), (10, 1.0)], pose_sitting)


CHARACTERS = [
    ("trooper", "trooper", {
        "main": (0.20, 0.42, 0.66),
        "secondary": (0.14, 0.22, 0.34),
        "pants": (0.16, 0.20, 0.26),
        "boots": (0.08, 0.09, 0.12),
        "accent": (0.22, 0.84, 0.98),
        "skin": (0.86, 0.70, 0.56),
        "visor": (0.08, 0.14, 0.22),
    }),
    ("ranger", "ranger", {
        "main": (0.28, 0.44, 0.26),
        "secondary": (0.42, 0.30, 0.18),
        "pants": (0.24, 0.22, 0.18),
        "boots": (0.14, 0.10, 0.08),
        "accent": (0.94, 0.70, 0.24),
        "skin": (0.88, 0.72, 0.58),
        "visor": (0.14, 0.18, 0.14),
    }),
    ("bot", "bot", {
        "main": (0.42, 0.48, 0.56),
        "secondary": (0.24, 0.28, 0.34),
        "pants": (0.20, 0.23, 0.28),
        "boots": (0.12, 0.13, 0.16),
        "accent": (0.98, 0.66, 0.16),
        "skin": (0.42, 0.48, 0.56),
        "visor": (0.08, 0.10, 0.14),
    }),
    ("medic", "medic", {
        "main": (0.88, 0.90, 0.94),
        "secondary": (0.22, 0.30, 0.40),
        "pants": (0.20, 0.26, 0.34),
        "boots": (0.12, 0.14, 0.18),
        "accent": (0.90, 0.20, 0.24),
        "skin": (0.90, 0.74, 0.60),
        "visor": (0.12, 0.20, 0.28),
    }),
]

for name, variant, palette in CHARACTERS:
    clear_scene()
    arm_obj, bone_names = create_armature(name)
    build_character_mesh(name, arm_obj, bone_names, palette, variant)
    create_animations(arm_obj)
    out_path = OUT_DIR / f"{name}.glb"
    bpy.ops.export_scene.gltf(
        filepath=str(out_path),
        export_format="GLB",
        export_animations=True,
        export_nla_strips=True,
        export_attributes=True,
        export_apply=False,
    )
    print(f"Exported {out_path} ({out_path.stat().st_size} bytes)")
