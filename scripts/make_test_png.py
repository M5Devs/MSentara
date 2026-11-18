#!/usr/bin/env python3
"""Generate a tiny synthetic RGBA PNG for manual/headless testing.

The image is produced from scratch (no copyrighted asset): an opaque red top
half and blue bottom half, so a rendered frame can be checked for the right
up/down orientation. Written with only the standard library, so the CI smoke
run needs no image toolchain.
"""
import struct
import sys
import zlib


def chunk(tag: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def write_png(path: str, width: int, height: int) -> None:
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type 0 (None) for this scanline
        top = y < height // 2
        for _ in range(width):
            raw += bytes((255, 0, 0, 255) if top else (0, 0, 255, 255))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )
    with open(path, "wb") as f:
        f.write(png)


def main() -> None:
    path = sys.argv[1] if len(sys.argv) > 1 else "test_layer.png"
    size = int(sys.argv[2]) if len(sys.argv) > 2 else 64
    write_png(path, size, size)
    print(f"wrote {path} ({size}x{size} RGBA PNG)")


if __name__ == "__main__":
    main()
