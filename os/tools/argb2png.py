#!/usr/bin/env python3
"""Convert the launcher's host-mode .argb dumps (16-byte header: magic 'OTVS',
u32 LE width, u32 LE height, u32 LE stride-bytes, then XRGB8888 = BGRX pixels) to PNG."""
import sys
from PIL import Image

def main():
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, "rb") as f:
        import struct
        magic, w, h, stride = struct.unpack("<4sIII", f.read(16))
        assert magic == b"OTVS", f"bad magic {magic}"
        data = f.read(h * stride)
    img = Image.frombytes("RGB", (w, h), data, "raw", "BGRX", 0, stride)
    img.save(dst)
    print(dst)

if __name__ == "__main__":
    main()
