"""Draws ZALoader's icon (a yellow hexagon outline with a bolt, on a dark rounded tile) and writes zaloader.ico
with 16, 24, 32, 48, 64 and 256 px images. Plain Python, no imaging library: 4x4 supersampled polygons.
Run: python make_icon.py  (writes zaloader.ico next to this script)"""
import math
import os
import struct

BG = (0x10, 0x11, 0x14)
ACCENT = (0xEB, 0xC9, 0x44)


def inside(poly, x, y):
    c = False
    for i in range(len(poly)):
        x1, y1 = poly[i]
        x2, y2 = poly[i - 1]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            c = not c
    return c


def hexagon(cx, cy, r):
    return [(cx + r * math.sin(math.radians(a)), cy - r * math.cos(math.radians(a))) for a in range(0, 360, 60)]


def render(n):
    s = 4  # supersampling
    cx = cy = n / 2
    outer, inner = hexagon(cx, cy, n * 0.40), hexagon(cx, cy, n * 0.40 - max(1.2, n * 0.075))
    # the bolt, on the logo's 24 x 24 grid
    k = n / 24.0
    bolt = [(x * k, y * k) for x, y in [(13.2, 5.5), (8, 13), (12, 13), (10.8, 18.5), (16, 11), (12, 11)]]
    radius = n * 0.2
    px = []
    for y in range(n):
        for x in range(n):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(s):
                for sx in range(s):
                    fx, fy = x + (sx + 0.5) / s, y + (sy + 0.5) / s
                    # rounded-square tile
                    dx = max(radius - fx, 0, fx - (n - radius))
                    dy = max(radius - fy, 0, fy - (n - radius))
                    if dx * dx + dy * dy > radius * radius:
                        continue
                    col = BG
                    if (inside(outer, fx, fy) and not inside(inner, fx, fy)) or inside(bolt, fx, fy):
                        col = ACCENT
                    acc[0] += col[0]; acc[1] += col[1]; acc[2] += col[2]; acc[3] += 255
            cov = acc[3] / (s * s * 255)
            if cov == 0:
                px.append((0, 0, 0, 0))
            else:
                w = s * s * cov
                px.append((int(acc[2] / w), int(acc[1] / w), int(acc[0] / w), int(cov * 255)))  # BGRA
    return px


def bmp_entry(n, px):
    header = struct.pack('<IiiHHIIiiII', 40, n, n * 2, 1, 32, 0, n * n * 4, 0, 0, 0, 0)
    rows = b''.join(bytes(c for p in px[y * n:(y + 1) * n] for c in p) for y in range(n - 1, -1, -1))  # bottom-up
    mask = b'\x00' * (((n + 31) // 32) * 4 * n)
    return header + rows + mask


def main():
    sizes = [16, 24, 32, 48, 64, 256]
    images = [bmp_entry(n, render(n)) for n in sizes]
    out = struct.pack('<HHH', 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    for n, img in zip(sizes, images):
        out += struct.pack('<BBBBHHII', n % 256, n % 256, 0, 0, 1, 32, len(img), offset)
        offset += len(img)
    out += b''.join(images)
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'zaloader.ico')
    with open(path, 'wb') as f:
        f.write(out)
    print('wrote', path, len(out), 'bytes')


if __name__ == '__main__':
    main()
