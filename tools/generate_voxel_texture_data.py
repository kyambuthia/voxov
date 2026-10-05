#!/usr/bin/env python3
"""Generate src/engine_render/voxel_texture_data.hpp.

Layers 0-2 come from the hand-authored 16 px tiles in assets/textures/voxel.
Layer 3 is reserved: the scene shader treats texture layer 3 as the composite
grass-side material (dirt with a turf lip), so it stores a dirt copy. Layers
4+ are building materials generated procedurally with a fixed seed, so
re-running the script is deterministic. Generated tiles are also written to
assets/textures/voxel for review.

Usage: python3 tools/generate_voxel_texture_data.py
"""

from __future__ import annotations

import random
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
TILE_DIR = ROOT / "assets" / "textures" / "voxel"
HEADER = ROOT / "src" / "engine_render" / "voxel_texture_data.hpp"
SIZE = 16


def clamp(value: float) -> int:
    return max(0, min(255, int(round(value))))


def shade(rgb, amount):
    return tuple(clamp(c + amount) for c in rgb)


def sand(rng: random.Random) -> Image.Image:
    image = Image.new("RGBA", (SIZE, SIZE))
    for y in range(SIZE):
        for x in range(SIZE):
            base = (206, 184, 128)
            noise = rng.randint(-9, 9)
            if rng.random() < 0.07:
                noise -= 22  # darker grains
            image.putpixel((x, y), shade(base, noise) + (255,))
    return image


def planks(rng: random.Random) -> Image.Image:
    image = Image.new("RGBA", (SIZE, SIZE))
    board_height = 4
    for y in range(SIZE):
        board = y // board_height
        joint = (board * 7 + 3) % SIZE
        for x in range(SIZE):
            base = (158, 112, 66)
            tone = (board * 13) % 17 - 8
            grain = rng.randint(-6, 6) + (6 if (x + board * 5) % 6 == 0 else 0)
            value = shade(base, tone + grain)
            if y % board_height == board_height - 1:
                value = shade(base, -48)  # seam between boards
            elif x == joint:
                value = shade(base, -36)  # butt joint
            image.putpixel((x, y), value + (255,))
    return image


def brick(rng: random.Random) -> Image.Image:
    image = Image.new("RGBA", (SIZE, SIZE))
    mortar = (176, 170, 156)
    row_height = 4
    brick_width = 8
    tones = {}
    for y in range(SIZE):
        row = y // row_height
        offset = 0 if row % 2 == 0 else brick_width // 2
        for x in range(SIZE):
            column = (x + offset) // brick_width
            key = (row, column % (SIZE // brick_width))
            if key not in tones:
                tones[key] = rng.randint(-14, 14)
            is_mortar = (y % row_height == row_height - 1 or
                         (x + offset) % brick_width == 0)
            if is_mortar:
                value = shade(mortar, rng.randint(-6, 6))
            else:
                value = shade((146, 66, 50), tones[key] + rng.randint(-7, 7))
            image.putpixel((x, y), value + (255,))
    return image


def snow(rng: random.Random) -> Image.Image:
    image = Image.new("RGBA", (SIZE, SIZE))
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b = (228, 236, 244)
            noise = rng.randint(-5, 4)
            if rng.random() < 0.06:
                r, g, b = (206, 220, 238)  # cool blue sparkle shadow
            image.putpixel((x, y), (clamp(r + noise), clamp(g + noise),
                                    clamp(b + noise), 255))
    return image


def load_tile(name: str) -> Image.Image:
    image = Image.open(TILE_DIR / f"{name}_16.png").convert("RGBA")
    if image.size != (SIZE, SIZE):
        raise SystemExit(f"{name}_16.png must be {SIZE}x{SIZE}")
    return image


def main() -> None:
    rng = random.Random(0x564F584F)
    generated = {
        "sand": sand(rng),
        "planks": planks(rng),
        "brick": brick(rng),
        "snow": snow(rng),
    }
    for name, image in generated.items():
        image.save(TILE_DIR / f"{name}_16.png")

    dirt = load_tile("dirt")
    layers = [
        load_tile("grass_top"),  # 0
        dirt,                    # 1
        load_tile("stone"),      # 2
        dirt,                    # 3: grass-side composite (shader sentinel)
        generated["sand"],       # 4
        generated["planks"],     # 5
        generated["brick"],      # 6
        generated["snow"],       # 7
    ]

    values = []
    for layer in layers:
        for y in range(SIZE):
            for x in range(SIZE):
                values.extend(layer.getpixel((x, y)))

    lines = []
    for start in range(0, len(values), 16):
        chunk = values[start:start + 16]
        lines.append("    " + ", ".join(str(v) for v in chunk) + ",")

    HEADER.write_text(
        "#pragma once\n\n"
        "#include <array>\n"
        "#include <cstdint>\n\n"
        "namespace voxov::voxel_textures {\n\n"
        f"inline constexpr int kWidth = {SIZE};\n"
        f"inline constexpr int kHeight = {SIZE};\n"
        f"inline constexpr int kLayerCount = {len(layers)};\n\n"
        "// Generated by tools/generate_voxel_texture_data.py from the tiles in\n"
        "// assets/textures/voxel. Keeping the tiny runtime texture array embedded\n"
        "// makes the same material path deterministic on desktop, WebAssembly,\n"
        "// and Android without platform-specific file I/O.\n"
        "inline constexpr std::array<std::uint8_t,\n"
        "                            kWidth * kHeight * kLayerCount * 4>\n"
        "    kRgba = {\n"
        + "\n".join(lines) + "\n"
        "};\n\n"
        "} // namespace voxov::voxel_textures\n"
    )
    print(f"wrote {HEADER.relative_to(ROOT)} with {len(layers)} layers")


if __name__ == "__main__":
    main()
