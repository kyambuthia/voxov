#!/usr/bin/env python3
"""Build the small embedded vegetation texture array from alpha PNGs."""

from pathlib import Path
import struct
import subprocess
import sys


def png_size(source: Path) -> tuple[int, int]:
    with source.open("rb") as handle:
        signature = handle.read(8)
        if signature != b"\x89PNG\r\n\x1a\n":
            raise ValueError(f"not a PNG: {source}")
        length = struct.unpack(">I", handle.read(4))[0]
        chunk_type = handle.read(4)
        if chunk_type != b"IHDR" or length < 8:
            raise ValueError(f"missing PNG IHDR: {source}")
        width, height = struct.unpack(">II", handle.read(8))
        return width, height


def rgba_pixels(source: Path) -> tuple[int, int, bytes]:
    width, height = png_size(source)
    result = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", str(source), "-f", "rawvideo",
         "-pix_fmt", "rgba", "pipe:1"],
        check=True,
        stdout=subprocess.PIPE,
    )
    expected = width * height * 4
    if len(result.stdout) != expected:
        raise ValueError(f"unexpected decoded size for {source}")
    return width, height, result.stdout


def make_layer(source: Path, size: int = 64) -> bytes:
    width, height, pixels = rgba_pixels(source)
    return make_layer_from_pixels(width, height, pixels, size)


def make_layer_from_pixels(width: int, height: int, pixels: bytes,
                           size: int = 64) -> bytes:
    visible = [
        (x, y)
        for y in range(height)
        for x in range(width)
        if pixels[(y * width + x) * 4 + 3] > 0
    ]
    if not visible:
        raise ValueError("no visible pixels in texture frame")
    min_x = min(x for x, _ in visible)
    max_x = max(x for x, _ in visible)
    min_y = min(y for _, y in visible)
    max_y = max(y for _, y in visible)
    subject_width = max_x - min_x + 1
    subject_height = max_y - min_y + 1
    scale = min((size * 0.86) / subject_width,
                (size * 0.92) / subject_height)
    scaled_width = max(1, round(subject_width * scale))
    scaled_height = max(1, round(subject_height * scale))
    layer = bytearray(size * size * 4)
    left = (size - scaled_width) // 2
    top = size - scaled_height - 2
    for y in range(scaled_height):
        source_y = min(subject_height - 1,
                        int(y * subject_height / scaled_height)) + min_y
        for x in range(scaled_width):
            source_x = min(subject_width - 1,
                            int(x * subject_width / scaled_width)) + min_x
            source_offset = (source_y * width + source_x) * 4
            layer_offset = ((top + y) * size + left + x) * 4
            layer[layer_offset:layer_offset + 4] = pixels[
                source_offset:source_offset + 4]
    return bytes(layer)


def make_octahedral_layers(source: Path, size: int = 64) -> list[bytes]:
    """Slice a 4x2 yaw atlas into its eight texture-array layers."""
    width, height, pixels = rgba_pixels(source)
    if width % 4 != 0 or height % 2 != 0:
        raise ValueError(f"atlas must divide into a 4x2 grid: {source}")
    frame_width = width // 4
    frame_height = height // 2
    layers = []
    for row in range(2):
        for column in range(4):
            frame = bytearray(frame_width * frame_height * 4)
            for y in range(frame_height):
                source_start = ((row * frame_height + y) * width +
                                column * frame_width) * 4
                target_start = y * frame_width * 4
                frame[target_start:target_start + frame_width * 4] = pixels[
                    source_start:source_start + frame_width * 4]
            layers.append(make_layer_from_pixels(frame_width, frame_height,
                                                 bytes(frame), size))
    return layers


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: generate_vegetation_texture_data.py <asset-root> <output.hpp>")
    root = Path(sys.argv[1])
    output = Path(sys.argv[2])
    # Layers 0..5 preserve the existing small crossed-card vegetation.
    # Every following group is a 4x2 atlas unpacked into eight directional
    # frames. The shader picks the best frame from the camera direction.
    pixel_root = root / "voxel"
    sources = [
        pixel_root / "grass/transparent/01-apparel.png",
        pixel_root / "grass/transparent/03-desk.png",
        pixel_root / "foliage/transparent/01-apparel.png",
        pixel_root / "foliage/transparent/03-desk.png",
        pixel_root / "flowers/transparent/01-apparel.png",
        pixel_root / "flowers/transparent/02-carry.png",
    ]
    layers = [make_layer(source) for source in sources]
    impostor_root = root / "impostors" / "transparent"
    for atlas in [
        impostor_root / "temperate_grass_octa_4x2.png",
        impostor_root / "meadow_wildflowers_octa_4x2.png",
        impostor_root / "woodland_shrub_octa_4x2.png",
        impostor_root / "forest_fern_octa_4x2.png",
    ]:
        layers.extend(make_octahedral_layers(atlas))
    values = ",\n".join(
        "    " + ", ".join(str(byte) for byte in layers[layer][offset:offset + 16])
        for layer in range(len(layers))
        for offset in range(0, len(layers[layer]), 16)
    )
    output.write_text(
        "#pragma once\n\n"
        "#include <array>\n#include <cstdint>\n\n"
        "namespace voxov::vegetation_textures {\n\n"
        "inline constexpr int kWidth = 64;\n"
        "inline constexpr int kHeight = 64;\n"
        f"inline constexpr int kLayerCount = {len(layers)};\n\n"
        "inline constexpr std::array<std::uint8_t, kWidth * kHeight * kLayerCount * 4>\n"
        "    kRgba = {\n"
        f"{values}\n"
        "};\n\n} // namespace voxov::vegetation_textures\n"
    )


if __name__ == "__main__":
    main()
