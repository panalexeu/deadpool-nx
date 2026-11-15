#!/usr/bin/env python3
"""Compare the raw scene depth dumps of one frame dump (volcado_NN/*.z24s8.bin):
  a  = the game's own depth copy right after the prepass (the 180D0000-style resolve, earliest depth dump)
  b  = DEB0Bxxx: the scene depth just before the base pass (before the depth-only clear)
  c  = DEC0Cxxx: the scene depth at the end of the frame (the base pass's own depth)
Prints, for each pair, how many pixels are equal, nearer, farther (reverse Z: larger = nearer) and the size of the
differences in 24-bit depth units. Usage: compare_depth.py volcado_NN"""
import glob, os, re, sys
import numpy as np

def load(path):
    m = re.search(r'_(\d+)x(\d+)_g', os.path.basename(path))
    w, h = int(m[1]), int(m[2])
    raw = np.fromfile(path, '<u4')[: w * h].reshape(h, w)
    return raw >> 8, raw & 0xFF, w, h

def compare(name, x, y):
    h = min(x.shape[0], y.shape[0]); w = min(x.shape[1], y.shape[1])
    x = x[:h, :w].astype(np.int64); y = y[:h, :w].astype(np.int64)
    d = y - x
    both = (x > 0) & (y > 0)
    print(f'{name}: {h}x{w}, both written {both.sum()}')
    if both.sum() == 0:
        return
    dd = d[both]
    print(f'  equal {np.sum(dd == 0)}, second nearer {np.sum(dd > 0)}, second farther {np.sum(dd < 0)}')
    nz = dd[dd != 0]
    if nz.size:
        a = np.abs(nz)
        print(f'  |diff| units: min {a.min()} median {int(np.median(a))} p90 {int(np.percentile(a, 90))} max {a.max()}')
        print(f'  diff histogram (signed, clipped to +-64):', np.unique(np.clip(nz, -64, 64), return_counts=True))

folder = sys.argv[1]
files = sorted(glob.glob(os.path.join(folder, '*.z24s8.bin')))
for f in files: print(os.path.basename(f))
a = [f for f in files if re.search(r'_[0-9A-F]{8}_', os.path.basename(f)) and not re.search(r'_DE[BC]0', os.path.basename(f))]
b = [f for f in files if '_DEB0B' in f]
c = [f for f in files if '_DEC0C' in f]
A = load(a[0])[0] if a else None
B = load(b[0])[0] if b else None
C = load(c[0])[0] if c else None
for n, v in (('a', A), ('b', B), ('c', C)):
    if v is not None: print(n, 'range', v.min(), v.max(), 'nonzero', int((v > 0).sum()))
if A is not None and B is not None: compare('a (after prepass, game copy) vs b (before base pass)', A, B)
if B is not None and C is not None: compare('b (prepass depth) vs c (base pass own depth)', B, C)
if A is not None and C is not None: compare('a vs c', A, C)
