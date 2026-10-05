#!/usr/bin/env python3
"""Build the small embedded vegetation texture array from alpha PNGs."""

from pathlib import Path
import struct
import sys
import zlib


_PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
_CHANNELS = {0: 1, 2: 3, 4: 2, 6: 4}  # grey, RGB, grey+alpha, RGBA


def _unfilter(raw: bytes, width: int, height: int, bpp: int) -> bytearray:
    """Reverse PNG scanline filters (spec section 9)."""
    stride = width * bpp
    out = bytearray(height * stride)
    previous = bytearray(stride)
    offset = 0
    for y in range(height):
        kind = raw[offset]
        line = bytearray(raw[offset + 1:offset + 1 + stride])
        offset += 1 + stride
        if kind == 1:  # Sub
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif kind == 2:  # Up
            line = bytearray((a + b) & 0xFF for a, b in zip(line, previous))
        elif kind == 3:  # Average
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif kind == 4:  # Paeth
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = previous[i]
                c = previous[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                predictor = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + predictor) & 0xFF
        elif kind != 0:
            raise ValueError(f"unknown PNG filter {kind}")
        out[y * stride:(y + 1) * stride] = line
        previous = line
    return out


def rgba_pixels(source: Path) -> tuple[int, int, bytes]:
    """Decode an 8-bit, non-interlaced PNG to RGBA with the standard library.

    The build used to shell out to ffmpeg, which CI runners don't have.
    """
    data = source.read_bytes()
    if data[:8] != _PNG_SIGNATURE:
        raise ValueError(f"not a PNG: {source}")
    offset = 8
    width = height = depth = color_type = interlace = None
    palette = b""
    transparency = b""
    compressed = bytearray()
    while offset < len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        body = data[offset + 8:offset + 8 + length]
        offset += 12 + length
        if kind == b"IHDR":
            width, height, depth, color_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = body
        elif kind == b"tRNS":
            transparency = body
        elif kind == b"IDAT":
            compressed += body
        elif kind == b"IEND":
            break
    if width is None:
        raise ValueError(f"missing PNG IHDR: {source}")
    if depth != 8 or interlace != 0 or color_type not in (0, 2, 3, 4, 6):
        raise ValueError(
            f"unsupported PNG format (depth={depth}, type={color_type}, "
            f"interlace={interlace}): {source}")
    bpp = 1 if color_type == 3 else _CHANNELS[color_type]
    pixels = _unfilter(zlib.decompress(bytes(compressed)), width, height, bpp)
    if color_type == 6:
        return width, height, bytes(pixels)
    rgba = bytearray(width * height * 4)
    for i in range(width * height):
        if color_type == 3:
            index = pixels[i]
            rgba[i * 4:i * 4 + 3] = palette[index * 3:index * 3 + 3]
            rgba[i * 4 + 3] = (transparency[index]
                               if index < len(transparency) else 255)
        elif color_type == 2:
            rgba[i * 4:i * 4 + 3] = pixels[i * 3:i * 3 + 3]
            rgba[i * 4 + 3] = 255
        else:
            grey = pixels[i * bpp]
            rgba[i * 4:i * 4 + 3] = bytes((grey, grey, grey))
            rgba[i * 4 + 3] = pixels[i * 2 + 1] if color_type == 4 else 255
    return width, height, bytes(rgba)


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
