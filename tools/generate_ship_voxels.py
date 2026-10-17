#!/usr/bin/env python3
"""Generate the voxel spaceship models in assets/models/ships.

Ships are authored on a one-metre grid with hollow interiors. The runtime
currently renders their exposed faces and uses hull bounds for interaction
and clearance. Walkable interiors, per-voxel ship collision, and block editing
aboard a ship are future work; the mount points are authoring metadata.

Axes match `SpaceshipState`: +Z is the nose, +Y is the cabin roof, +X is right.
The MagicaVoxel export swaps to that format's Z-up convention on the way out.

Each ship writes two files:
  <name>.vox   - voxel data, openable in MagicaVoxel for hand editing
  <name>.json  - mount points (.vox cannot carry them) and the palette legend

Usage: python3.13 tools/generate_ship_voxels.py
"""

from __future__ import annotations

import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "assets" / "models" / "ships"

# Palette. Index 0 is empty in .vox, so materials start at 1. Colours match the
# reference art in assets/references/spaceships.
MATERIALS = {
    "hull":     (1, (0xC8, 0xCB, 0xD0), "light grey outer plating (courier)"),
    "panel":    (2, (0x4A, 0x4E, 0x55), "dark grey panel and greeble accents"),
    "glass":    (3, (0x18, 0x4E, 0x5C), "dark teal canopy and viewports"),
    "thruster": (4, (0xE8, 0x74, 0x1E), "orange engine nozzle, drives exhaust glow"),
    "deck":     (5, (0x6E, 0x73, 0x7B), "interior walkable floor"),
    "light":    (6, (0xF2, 0xE2, 0xC0), "warm interior lighting block"),
    "bone":     (7, (0xEA, 0xE6, 0xDB), "bone white outer plating (shrike)"),
    "cyan":     (8, (0x6E, 0xD8, 0xD0), "pale cyan canopy glass (shrike)"),
}


# ── Grid helpers ──────────────────────────────────────────────────────────


class Grid:
    """Sparse voxel grid keyed by (x, y, z) holding material names."""

    def __init__(self) -> None:
        self.cells: dict[tuple[int, int, int], str] = {}

    def fill(self, x0, x1, y0, y1, z0, z1, material) -> None:
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                for z in range(z0, z1 + 1):
                    self.cells[(x, y, z)] = material

    def carve(self, x0, x1, y0, y1, z0, z1) -> None:
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                for z in range(z0, z1 + 1):
                    self.cells.pop((x, y, z), None)

    def mirror_x(self, axis: int) -> None:
        """Reflect everything across the x=axis plane. Build one side, get both."""
        for (x, y, z), material in list(self.cells.items()):
            self.cells[(2 * axis - x, y, z)] = material

    def bounds(self):
        xs = [c[0] for c in self.cells]
        ys = [c[1] for c in self.cells]
        zs = [c[2] for c in self.cells]
        return (min(xs), max(xs)), (min(ys), max(ys)), (min(zs), max(zs))


def hollow(grid, sections, centre_x, cabin_z0, cabin_z1, skin) -> set:
    """Carve a cabin out of a stack of hull sections and lay its deck.

    The carve is inset from each section's own bounds rather than taken as one
    box, because the hull narrows toward the nose - a single carve wide enough
    for the midships would cut straight through the cockpit's side wall.

    Returns the cells it opened, which is the exact cabin volume. Describing
    the cabin with a bounding box instead reports open air under a rising nose
    as a hull breach.
    """
    carved = set()
    for z0, z1, half_width, y0, y1 in sections:
        lo, hi = max(z0, cabin_z0), min(z1, cabin_z1)
        if lo > hi or half_width < 2 or y1 - y0 < 3:
            continue
        x0 = centre_x - half_width + 1
        grid.carve(x0, centre_x, y0 + 1, y1 - 1, lo, hi)
        grid.fill(x0, centre_x, y0 + 1, y0 + 1, lo, hi, skin)
        for x in range(x0, centre_x + 1):
            for y in range(y0 + 1, y1):
                for z in range(lo, hi + 1):
                    carved.add((x, y, z))
    return carved


def mirrored(cells, axis: int) -> set:
    return cells | {(2 * axis - x, y, z) for x, y, z in cells}


# ── Courier ───────────────────────────────────────────────────────────────


def build_courier():
    """Two-crew interplanetary hauler, 19 x 10 x 28 voxels (metres).

    Proportions follow assets/references/spaceships/courier: long fuselage,
    raked canopy standing proud of the spine a quarter back from the nose,
    airlock amidships, cargo pod slung under the belly, twin nacelles riding
    above the wing roots at the stern.
    """
    g = Grid()
    c = 10
    cabin = (5, 22)

    # (z0, z1, half_width, y_min, y_max). One deep primary mass carries the
    # silhouette; width and crown step down toward the nose.
    sections = [
        (2, 9, 4, 2, 8),    # aft hold, widest and deepest
        (10, 17, 4, 2, 8),  # midships
        (18, 21, 3, 2, 8),  # shoulders pull in
        (22, 23, 3, 3, 7),  # crown starts dropping
        (24, 25, 2, 3, 6),  # nose block
        (26, 26, 1, 4, 6),
        (27, 27, 1, 4, 5),  # tip
    ]
    for z0, z1, half_width, y0, y1 in sections:
        g.fill(c - half_width, c, y0, y1, z0, z1, "hull")
    g.fill(7, c, 3, 7, 1, 1, "hull")                 # tail taper

    cabin_cells = hollow(g, sections, c, *cabin, "deck")
    g.fill(c, c, 7, 7, 8, 20, "light")

    # Canopy: a raked bubble standing proud of the spine, stepping down onto
    # the nose. Built after the carve so it stays solid.
    g.fill(8, c, 9, 9, 19, 22, "glass")
    g.fill(9, c, 8, 8, 23, 23, "glass")
    g.fill(9, c, 7, 7, 24, 25, "glass")              # forward windscreen
    g.fill(8, 8, 9, 9, 19, 22, "panel")              # canopy frame rail

    g.fill(6, 6, 4, 6, 12, 14, "panel")              # airlock
    g.fill(6, 6, 4, 4, 12, 14, "deck")

    # Greebles, kept to one- and two-voxel marks: at this scale a large dark
    # slab reads as a hole in the hull rather than as panelling.
    g.fill(6, 6, 8, 8, 5, 7, "panel")
    g.fill(6, 6, 6, 6, 17, 18, "panel")
    g.fill(8, 9, 8, 8, 9, 10, "panel")
    g.fill(8, 9, 8, 8, 15, 16, "panel")
    g.fill(7, 7, 2, 2, 4, 7, "panel")
    g.fill(6, 6, 3, 3, 19, 20, "glass")              # chine viewport

    # Cargo pod, inset in x so it reads as a separate body.
    g.fill(8, c, 0, 1, 9, 18, "hull")
    g.fill(8, 8, 1, 1, 11, 16, "panel")
    g.fill(8, c, 1, 1, 19, 19, "panel")

    for i, x in enumerate(range(1, 7)):              # swept wing
        g.fill(x, x, 4, 5, max(5 - i, 1), 13 - i, "hull")
    g.fill(1, 2, 4, 5, 1, 4, "panel")                # wingtip cap
    g.fill(1, 1, 5, 5, 1, 2, "thruster")             # position light

    # Nacelle, half sunk into the wing root so it reads as part of the hull.
    g.fill(3, 6, 5, 8, 1, 7, "hull")
    g.fill(3, 6, 5, 8, 8, 8, "panel")
    g.fill(3, 6, 5, 8, 0, 0, "thruster")
    g.fill(4, 5, 9, 9, 2, 5, "panel")

    g.mirror_x(c)
    mounts = {
        "pilot_seat": [c + 1, 4, 21], "copilot_seat": [c - 1, 4, 21],
        "airlock": [6, 4, 13], "cargo_hold": [c, 4, 7],
        "docking_port": [c, 5, 27],
        "thrusters": [[c - 6, 6, 0], [c + 6, 6, 0]],
    }
    return g, mounts, mirrored(cabin_cells, c)


# ── Shrike ────────────────────────────────────────────────────────────────


def build_shrike():
    """Single-seat manta interceptor, 51 x 15 x 60 voxels (metres).

    Proportions follow assets/references/spaceships/shrike, which in turn
    reinterprets the supplied concept art. Built at 60 voxels rather than the
    courier's 28 because the character of the craft is in thin blade wings and
    a needle nose: at half this length both coarsen into stubs.

    The wing is the whole ship. It sweeps hard aft from a point near the nose
    out to tips that trail past the engines, drops in anhedral as it goes
    outboard, then kicks the last few metres back up into winglets.
    """
    g = Grid()
    c = 25
    cabin = (22, 46)

    # (z0, z1, half_width, y_min, y_max), stern to nose.
    sections = [
        (8, 19, 3, 5, 10),   # stern, between the engine pods
        (20, 33, 3, 4, 11),  # deepest point, cabin
        (34, 43, 3, 5, 11),  # cockpit
        (44, 49, 2, 6, 10),  # shoulders pull in behind the nose
        (50, 53, 2, 6, 9),
        (54, 56, 1, 7, 9),
        (57, 59, 0, 7, 8),   # needle tip
    ]
    for z0, z1, half_width, y0, y1 in sections:
        g.fill(c - half_width, c, y0, y1, z0, z1, "bone")

    cabin_cells = hollow(g, sections, c, *cabin, "deck")
    g.fill(c, c, 10, 10, 24, 40, "light")

    # Canopy: a slim blister let into the spine rather than standing on it -
    # this is a cockpit, not a cabin with a view.
    g.fill(c - 2, c, 11, 12, 40, 48, "cyan")
    g.fill(c - 3, c - 3, 11, 12, 40, 48, "panel")    # canopy frame
    g.fill(c - 2, c, 10, 10, 49, 51, "cyan")         # forward windscreen

    # Engine pod, outboard of the fuselage and forward of the wing trailing
    # edge, merging into the wing root where the two meet.
    g.fill(c - 10, c - 5, 6, 11, 10, 42, "bone")
    g.fill(c - 10, c - 5, 6, 11, 8, 9, "thruster")   # nozzle
    g.fill(c - 10, c - 5, 6, 11, 43, 44, "panel")    # intake lip
    g.fill(c - 9, c - 6, 12, 12, 18, 34, "panel")    # pod spine
    g.fill(c - 11, c - 11, 8, 9, 20, 36, "panel")    # outboard flank

    # Delta wing. Chord shrinks and shifts aft going outboard; the section
    # drops in anhedral to d=20 then kicks up into a winglet.
    #
    # Each column is spanned vertically to meet its inboard neighbour. Filling
    # only each column's own y leaves the stations corner-adjacent as the
    # section steps down, so the wing is 22 disconnected blades rather than a
    # surface - it reads fine in plan and falls off the ship in six-connected
    # space.
    previous = None
    for d in range(4, 26):
        x = c - d
        leading = int(48 - 1.5 * d)
        # The trailing edge rakes forward as the leading edge rakes aft, so the
        # chord closes toward a point instead of ending on a blunt tip.
        trailing = max(0, round(0.3 * d) - 1)
        if leading <= trailing:
            continue
        y = 7 - round(0.18 * d) if d <= 20 else 4 + (d - 20)
        thickness = 2 if d < 16 else 1
        lo, hi = y, y + thickness - 1
        if previous is not None:
            lo, hi = min(lo, previous[0]), max(hi, previous[1])
        previous = (y, y + thickness - 1)

        g.fill(x, x, lo, hi, trailing, leading, "bone")
        g.fill(x, x, lo, hi, leading - 1, leading, "panel")   # leading edge
        g.fill(x, x, lo, lo, trailing, trailing + 1, "panel")  # trailing edge

    # Folded landing claws tucked under the belly, each hung on short struts.
    # The hull floor steps up from y=4 to y=5 under the cockpit and the pods
    # bottom out at y=6, so a blade laid at a single height reaches nothing.
    for d, strut_top in ((3, 5), (6, 7)):
        x = c - d
        g.fill(x, x, 3, 3, 28, 40, "panel")          # blade
        g.fill(x, x, 3, strut_top, 30, 31, "panel")  # forward strut
        g.fill(x, x, 3, strut_top, 37, 38, "panel")  # aft strut
    g.fill(c - 2, c, 3, 3, 26, 27, "panel")

    g.mirror_x(c)
    mounts = {
        "pilot_seat": [c, 6, 43],
        "docking_port": [c, 7, 59],
        "thrusters": [[c - 7, 8, 8], [c + 7, 8, 8]],
    }
    return g, mounts, mirrored(cabin_cells, c)


SHIPS = {"courier": build_courier, "shrike": build_shrike}


# ── Validation ────────────────────────────────────────────────────────────


def check_shell(grid, cabin_cells) -> tuple[int, int]:
    """Flood air in from outside the bounding box and report the hull's state.

    Returns (enclosed_cells, breaches). A hull is sound when the cabin is
    enclosed: air the flood cannot reach is interior volume. Any cabin cell the
    flood does reach is a hole in the plating - the kind of gap a three-quarter
    render hides and a side elevation only hints at.

    `cabin_cells` is the exact set the carve opened, not a bounding box: a box
    drawn round a cabin whose floor rises toward the nose also encloses open
    air beneath it, and reports every cell of it as a breach.
    """
    (x0, x1), (y0, y1), (z0, z1) = grid.bounds()
    lo = (x0 - 1, y0 - 1, z0 - 1)
    hi = (x1 + 1, y1 + 1, z1 + 1)

    outside = set()
    stack = [lo]
    while stack:
        cell = stack.pop()
        if cell in outside or cell in grid.cells:
            continue
        x, y, z = cell
        if not (lo[0] <= x <= hi[0] and lo[1] <= y <= hi[1] and lo[2] <= z <= hi[2]):
            continue
        outside.add(cell)
        stack += [(x + 1, y, z), (x - 1, y, z), (x, y + 1, z),
                  (x, y - 1, z), (x, y, z + 1), (x, y, z - 1)]

    enclosed = sum(1 for x in range(x0, x1 + 1)
                   for y in range(y0, y1 + 1)
                   for z in range(z0, z1 + 1)
                   if (x, y, z) not in grid.cells and (x, y, z) not in outside)
    breaches = sum(1 for cell in cabin_cells
                   if cell not in grid.cells and cell in outside)
    return enclosed, breaches


def check_connected(grid) -> int:
    """Count solid voxels not reachable from the largest body.

    Six-connected, matching how the physics treats a voxel volume. A swept
    surface built column by column is the usual way to get this wrong: step
    the section down one voxel per station and neighbouring columns touch only
    at a corner, so the wing is detached while still looking correct in plan.
    """
    remaining = set(grid.cells)
    largest = 0
    while remaining:
        stack = [next(iter(remaining))]
        size = 0
        while stack:
            x, y, z = cell = stack.pop()
            if cell not in remaining:
                continue
            remaining.discard(cell)
            size += 1
            stack += [(x + 1, y, z), (x - 1, y, z), (x, y + 1, z),
                      (x, y - 1, z), (x, y, z + 1), (x, y, z - 1)]
        largest = max(largest, size)
    return len(grid.cells) - largest


# ── MagicaVoxel export ────────────────────────────────────────────────────


def chunk(tag: bytes, content: bytes, children: bytes = b"") -> bytes:
    return tag + struct.pack("<ii", len(content), len(children)) + content + children


def write_vox(path: Path, grid: Grid) -> tuple[int, int, int]:
    """Write a MagicaVoxel .vox file.

    .vox is Z-up and indexed from the minimum corner, so game (x, y, z)
    becomes vox (x, z, y) after shifting the model to the origin.
    """
    (x0, x1), (y0, y1), (z0, z1) = grid.bounds()
    size = (x1 - x0 + 1, z1 - z0 + 1, y1 - y0 + 1)  # vox x, y, z

    voxels = bytearray()
    for (x, y, z), material in sorted(grid.cells.items()):
        index = MATERIALS[material][0]
        voxels += struct.pack("<BBBB", x - x0, z - z0, y - y0, index)

    size_chunk = chunk(b"SIZE", struct.pack("<iii", *size))
    xyzi_chunk = chunk(b"XYZI", struct.pack("<i", len(grid.cells)) + bytes(voxels))

    # RGBA holds 256 entries; .vox reads palette index i from entry i-1.
    palette = bytearray()
    by_index = {spec[0]: spec[1] for spec in MATERIALS.values()}
    for i in range(1, 257):
        r, g, b = by_index.get(i, (0, 0, 0))
        palette += struct.pack("<BBBB", r, g, b, 255 if i in by_index else 0)
    rgba_chunk = chunk(b"RGBA", bytes(palette))

    body = size_chunk + xyzi_chunk + rgba_chunk
    path.write_bytes(b"VOX " + struct.pack("<i", 150) + chunk(b"MAIN", b"", body))
    return size


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    failed = False
    for name, build in SHIPS.items():
        grid, mounts, cabin_cells = build()
        size = write_vox(OUT_DIR / f"{name}.vox", grid)
        counts: dict[str, int] = {}
        for material in grid.cells.values():
            counts[material] = counts.get(material, 0) + 1

        sidecar = {
            "name": name,
            "voxel_size_meters": 1.0,
            "axes": "+Z nose, +Y roof, +X right; origin at the minimum corner",
            "size_voxels": {"x": size[0], "y": size[2], "z": size[1]},
            "mounts": mounts,
            "palette": {
                material: {"index": index, "rgb": list(rgb), "role": role}
                for material, (index, rgb, role) in MATERIALS.items()
                if material in counts
            },
        }
        (OUT_DIR / f"{name}.json").write_text(json.dumps(sidecar, indent=2) + "\n")

        enclosed, breaches = check_shell(grid, cabin_cells)
        loose = check_connected(grid)
        print(f"{name}: {size[0]} x {size[2]} x {size[1]} (w x h x l), "
              f"{len(grid.cells)} voxels")
        print("    " + "  ".join(f"{m} {counts[m]}"
                                 for m in sorted(counts, key=lambda m: -counts[m])))
        print(f"    cabin {enclosed} m3 enclosed, {breaches} hull breaches, "
              f"{loose} detached voxels")
        if breaches or loose:
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
