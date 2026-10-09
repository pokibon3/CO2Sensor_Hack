#!/usr/bin/env python3
"""Convert binary PPM (P6) files to PNG without extra packages."""
import struct, sys, zlib


def convert(src, dst):
    data = open(src, 'rb').read()
    parts = data.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    raw = b''.join(b'\0' + px[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(dst, 'wb').write(png)


for path in sys.argv[1:]:
    convert(path, path[:-4] + '.png')
