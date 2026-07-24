"""Generate a simple multi-size .ico: teal rounded square with white 'Q'."""
from pathlib import Path
import struct
import zlib


def png_chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def write_png(size: int, pixels_rgba: bytes) -> bytes:
    assert len(pixels_rgba) == size * size * 4
    raw = b""
    stride = size * 4
    for y in range(size):
        raw += b"\x00" + pixels_rgba[y * stride : (y + 1) * stride]
    compressed = zlib.compress(raw, 9)
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", compressed)
        + png_chunk(b"IEND", b"")
    )


def draw_icon(size: int) -> bytes:
    px = bytearray(size * size * 4)
    bg = (0x12, 0x5A, 0x6E, 255)
    fg = (0xF2, 0xF7, 0xFA, 255)
    accent = (0x3D, 0xDC, 0x97, 255)

    def setp(x, y, c):
        if 0 <= x < size and 0 <= y < size:
            i = (y * size + x) * 4
            px[i : i + 4] = bytes(c)

    cx = cy = (size - 1) / 2.0
    for y in range(size):
        for x in range(size):
            dx, dy = x - cx, y - cy
            nx, ny = abs(dx) / (size * 0.46), abs(dy) / (size * 0.46)
            m = max(nx, ny)
            if m <= 1.0:
                setp(x, y, bg)
            elif m <= 1.08:
                setp(x, y, (bg[0], bg[1], bg[2], 180))

    thick = max(1, size // 10)
    for y in range(size):
        for x in range(size):
            dx, dy = x - cx, y - cy
            d = (dx * dx + dy * dy) ** 0.5
            outer = size * 0.28
            inner = outer - thick
            if inner <= d <= outer:
                i = (y * size + x) * 4
                if px[i + 3] > 100:
                    setp(x, y, fg)

    for t in range(thick + size // 8):
        x = int(cx + size * 0.10 + t * 0.55)
        y = int(cy + size * 0.10 + t * 0.55)
        for oy in range(-(thick // 2), thick // 2 + 1):
            for ox in range(-(thick // 2), thick // 2 + 1):
                setp(x + ox, y + oy, accent)

    return write_png(size, bytes(px))


def build_ico(pngs):
    count = len(pngs)
    header = struct.pack("<HHH", 0, 1, count)
    offset = 6 + 16 * count
    entries = b""
    blobs = b""
    for size, data in pngs:
        w = 0 if size >= 256 else size
        h = 0 if size >= 256 else size
        entries += struct.pack("<BBBBHHII", w, h, 0, 0, 1, 32, len(data), offset)
        blobs += data
        offset += len(data)
    return header + entries + blobs


def main():
    out = Path(__file__).resolve().parent.parent / "resources" / "app.ico"
    pngs = [(s, draw_icon(s)) for s in (16, 32, 48, 256)]
    data = build_ico(pngs)
    out.write_bytes(data)
    print("wrote", out, "bytes", len(data))


if __name__ == "__main__":
    main()
