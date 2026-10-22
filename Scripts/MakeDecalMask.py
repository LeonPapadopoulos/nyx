"""Makes Assets/Textures/DecalRing.png, the masked decal material's sample mask: a ring, white on
black, as a greyscale PNG.

The ring's edges fade over a few texels, from the distance to the ring, so that the masked decal
material (Assets/Shaders/MaskedDecal.slang) finds the edge at grey 0.5 and draws it smooth at any
size. Needs only Python 3:

    python Scripts/MakeDecalMask.py
"""

import math
import pathlib
import struct
import zlib

SIZE = 256
RADIUS = 92.0
# Half the ring's width, and how far its edge fades on each side, in texels
HALF_WIDTH = 10.0
FADE = 3.0


def grey(px, py):
    # 0.5 at the ring's edge, white inside it, black beyond the fade
    centre = SIZE / 2.0
    edge = abs(math.hypot(px - centre, py - centre) - RADIUS) - HALF_WIDTH
    return max(0.0, min(1.0, 0.5 - edge / (2.0 * FADE)))


def png(width, height, rows):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    header = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)  # 8-bit greyscale
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def main():
    rows = [[round(grey(x + 0.5, y + 0.5) * 255.0) for x in range(SIZE)] for y in range(SIZE)]
    path = pathlib.Path(__file__).resolve().parent.parent / "Assets" / "Textures" / "DecalRing.png"
    path.write_bytes(png(SIZE, SIZE, rows))
    print("Wrote", path)


if __name__ == "__main__":
    main()
