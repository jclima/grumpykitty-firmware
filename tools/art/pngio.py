# SPDX-License-Identifier: MIT
# Copyright (c) 2026 JC Lima
"""Minimal stdlib PNG reader/writer (8-bit RGB/RGBA, non-interlaced)."""
import struct
import zlib


def _chunk(tag, data):
    c = struct.pack(">I", len(data)) + tag + data
    return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png(path, w, h, rgba):
    """rgba: bytes of w*h*4."""
    assert len(rgba) == w * h * 4
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgba[y * w * 4:(y + 1) * w * 4]
    out = b"\x89PNG\r\n\x1a\n"
    out += _chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    out += _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    out += _chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(out)


def read_png(path):
    """Return (w, h, rgba bytes). Handles RGB/RGBA 8-bit, all filters."""
    with open(path, "rb") as f:
        d = f.read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG: %s" % path)
    pos, idat, hdr = 8, b"", None
    while pos < len(d):
        n, = struct.unpack(">I", d[pos:pos + 4])
        tag = d[pos + 4:pos + 8]
        body = d[pos + 8:pos + 8 + n]
        pos += 12 + n
        if tag == b"IHDR":
            hdr = struct.unpack(">IIBBBBB", body)
        elif tag == b"IDAT":
            idat += body
    w, h, depth, ctype, _, _, interlace = hdr
    if depth != 8 or ctype not in (2, 6) or interlace:
        raise ValueError("unsupported PNG (depth=%d type=%d interlace=%d)" % (depth, ctype, interlace))
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * bpp
    prev = bytearray(stride)
    out = bytearray()
    p = 0
    for _ in range(h):
        ft = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ft == 1:
                line[i] = (line[i] + a) & 255
            elif ft == 2:
                line[i] = (line[i] + b) & 255
            elif ft == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif ft == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        out += line
        prev = line
    if bpp == 3:
        rgba = bytearray()
        for i in range(0, len(out), 3):
            rgba += out[i:i + 3] + b"\xff"
        return w, h, bytes(rgba)
    return w, h, bytes(out)
