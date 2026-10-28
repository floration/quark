#!/usr/bin/env python3
"""RENDER SCALE check (ctest "renderscale", tools/release_check.sh): Bastion hosted headless at 50%, 100% and 200%
from the same fixed camera, one screenshot each. Each must be the window's size (1280 x 720, whatever the scale), the
HUD drawn at the window's own resolution (the health number's white fill the same pixels at every scale: it would blur
if it were stretched with the world), the world drawn at another resolution (its pixels differ from 100%'s), and
render_scale still in quark.cfg after the game wrote it back.

usage: tools/renderscale_check.py <path to quark>   (needs a display: Xvfb; exit 77 = skipped without one)
"""
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

W, H = 1280, 720
HEALTH = (20, 640, 110, 685)   # the health number's box at 720p (x0, y0, x1, y1): bottom left
WORLD = (300, 200, 980, 560)   # a patch of the world, clear of the HUD


def read_png(path):
    """quark's own screenshots: 8-bit RGB, one IDAT, filter 0 on every row (Renderer::screenshot)"""
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', 'not a PNG'
    pos, w, h, idat = 8, 0, 0, b''
    while pos < len(d):
        n, kind = struct.unpack('>I4s', d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h = struct.unpack('>II', body[:8])
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    rows = [raw[y * (w * 3 + 1) + 1:(y + 1) * (w * 3 + 1)] for y in range(h)]
    return w, h, rows


def white(rows, box):
    x0, y0, x1, y1 = box
    return {(x, y) for y in range(y0, y1) for x in range(x0, x1)
            if min(rows[y][x * 3:x * 3 + 3]) >= 250}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    quark = os.path.abspath(sys.argv[1])
    if not os.environ.get('DISPLAY'):
        print('renderscale: no DISPLAY - skipped (run it under Xvfb)')
        return 77
    tmp = tempfile.mkdtemp(prefix='quark_renderscale_')
    port = 28000 + random.randrange(1000)   # (a port of its own: other games on this machine don't collide)
    fails, shots = [], {}
    for pct in (50, 100, 200):
        data = os.path.join(tmp, str(pct))
        os.makedirs(data)
        with open(os.path.join(data, 'quark.cfg'), 'w') as f:
            f.write('render_scale=%d\nwidth=%d\nheight=%d\nhost_port=%d\n' % (pct, W, H, port + pct))
        png = os.path.join(tmp, 'shot%d.png' % pct)
        cmd = [quark, '--data-dir', data, '--host', 'ffa', '--map', 'bastion', '--cam', '-12', '1.7', '26', '0', '-5',
               '--screenshot', png]
        try:
            r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
        except subprocess.TimeoutExpired:
            fails.append('%d%%: timed out' % pct)
            continue
        if r.returncode != 0 or not os.path.exists(png):
            fails.append('%d%%: quark exited %d with no screenshot' % (pct, r.returncode))
            continue
        w, h, rows = read_png(png)
        if (w, h) != (W, H):
            fails.append('%d%%: the screenshot is %dx%d, not the window\'s %dx%d' % (pct, w, h, W, H))
            continue
        shots[pct] = rows
        cfg = open(os.path.join(data, 'quark.cfg')).read()
        if 'render_scale=%d\n' % pct not in cfg:
            fails.append('%d%%: quark.cfg lost render_scale (round trip)' % pct)
        print('renderscale: %d%% -> %dx%d screenshot' % (pct, w, h))
    if 100 in shots:
        base = white(shots[100], HEALTH)
        if len(base) < 50:
            fails.append('100%%: the health number wasn\'t found (%d white pixels)' % len(base))
        for pct in (50, 200):
            if pct not in shots:
                continue
            s = white(shots[pct], HEALTH)
            same = len(base & s) / max(1, len(base | s))
            print('renderscale: %d%% HUD: %d of %d white pixels match 100%%\'s (%.1f%%)' % (pct, len(base & s), len(base | s), same * 100))
            if same < 0.98:
                fails.append('%d%%: the HUD isn\'t drawn at the window\'s resolution (%.1f%% of the health number matches)' % (pct, same * 100))
            x0, y0, x1, y1 = WORLD
            diff = sum(1 for y in range(y0, y1) for x in range(x0 * 3, x1 * 3) if abs(shots[pct][y][x] - shots[100][y][x]) > 24)
            print('renderscale: %d%% world: %d channel values differ from 100%%' % (pct, diff))
            if diff < 200:
                fails.append('%d%%: the world looks the same as at 100%% (not drawn at another resolution?)' % pct)
    for f in fails:
        print('renderscale: FAIL ' + f)
    if not fails:
        shutil.rmtree(tmp, ignore_errors=True)
        print('renderscale: ok')
    else:
        print('renderscale: screenshots kept in ' + tmp)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
