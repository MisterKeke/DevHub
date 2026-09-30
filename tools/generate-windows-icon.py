"""Generate DevHub's multi-resolution Windows ICO using only Python's stdlib."""

from __future__ import annotations

import struct
from pathlib import Path


OUTPUT = Path(__file__).resolve().parents[1] / "resources" / "branding" / "devhub.ico"
SIZES = (16, 24, 32, 48, 64, 128, 256)


def pixel(size: int, x: int, y: int) -> tuple[int, int, int, int]:
    scale = size / 256
    px, py = x / scale, y / scale
    cx, cy = 128.0, 128.0

    # Rounded dark tile and a restrained plum outline.
    nearest_x = min(max(px, 18.0), 238.0)
    nearest_y = min(max(py, 18.0), 238.0)
    inside = (px - nearest_x) ** 2 + (py - nearest_y) ** 2 <= 38.0**2
    if not inside:
        return 0, 0, 0, 0
    border = px < 24 or px > 232 or py < 24 or py > 232
    background = (81, 48, 71, 255) if border else (23, 19, 26, 255)

    # A bold D mark, matching the project's dark-plum and magenta branding.
    left_stem = 66 <= px <= 98 and 58 <= py <= 198
    top_bar = 66 <= px <= 152 and 58 <= py <= 90
    bottom_bar = 66 <= px <= 152 and 166 <= py <= 198
    dx_outer = (px - 126) / 66
    dy_outer = (py - 128) / 70
    outer_curve = px >= 96 and dx_outer * dx_outer + dy_outer * dy_outer <= 1
    dx_inner = (px - 126) / 34
    dy_inner = (py - 128) / 37
    inner_curve = px >= 98 and dx_inner * dx_inner + dy_inner * dy_inner < 1
    if (left_stem or top_bar or bottom_bar or outer_curve) and not inner_curve:
        return 255, 45, 122, 255
    return background


def image_entry(size: int) -> bytes:
    rgba = [[pixel(size, x, y) for x in range(size)] for y in range(size)]
    xor = bytearray()
    for row in reversed(rgba):
        for red, green, blue, alpha in row:
            xor.extend((blue, green, red, alpha))

    # 32-bit icons still carry the legacy AND mask. All pixels are opaque or
    # alpha-transparent, so the mask is zero-filled and row-aligned.
    mask_stride = ((size + 31) // 32) * 4
    and_mask = bytes(mask_stride * size)
    bitmap_header = struct.pack(
        "<IIIHHIIIIII", 40, size, size * 2, 1, 32, 0,
        len(xor), 0, 0, 0, 0
    )
    return bitmap_header + xor + and_mask


def main() -> None:
    entries = [image_entry(size) for size in SIZES]
    directory = bytearray(struct.pack("<HHH", 0, 1, len(SIZES)))
    offset = 6 + 16 * len(SIZES)
    for size, data in zip(SIZES, entries, strict=True):
        dimension = 0 if size == 256 else size
        directory.extend(struct.pack(
            "<BBBBHHII", dimension, dimension, 0, 0, 1, 32, len(data), offset
        ))
        offset += len(data)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(directory + b"".join(entries))
    print(f"Wrote {OUTPUT}")


if __name__ == "__main__":
    main()
