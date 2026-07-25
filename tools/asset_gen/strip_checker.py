#!/usr/bin/env python3
"""Strip baked transparency-checkerboard backgrounds out of game art.

Some generated/exported PNGs have the editor's transparency checkerboard
(the two-tone gray grid) flattened into the image as real opaque pixels, so
in game the sprite renders sitting on a gray-and-white tiled plate. This
removes it without touching the artwork.

How it decides what is background:
  1. The checker is exactly TWO flat neutral-gray levels. Find them as the two
     dominant levels among opaque, neutral, light pixels.
  2. Mark pixels matching either level.
  3. Keep only the parts of that mask CONNECTED to the image's existing
     transparent area. This is what protects the art: a white horse or gray
     armour matches the levels by colour, but it is enclosed by real artwork,
     so it is never reached from outside.
  4. Grow one more step into the in-between values produced when the sheet was
     resampled (the soft edge between two checker squares), again only
     outward from already-cleared background.

Usage:
    python tools/asset_gen/strip_checker.py [--dry-run] FILE.png [FILE.png ...]
    python tools/asset_gen/strip_checker.py --scan assets      # just report

Always eyeball the result: this rewrites the PNGs in place.
"""
import argparse
import glob
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

NEIGH = np.ones((3, 3), bool)


def _levels(a, g, neutral):
    """The two dominant light neutral-gray levels, or None."""
    cand = (a > 200) & neutral & (g >= 165)
    if cand.sum() < 800:
        return None
    hist = np.bincount((g[cand] // 8).astype(int), minlength=32).astype(float)
    top = np.argsort(hist)[::-1][:2]
    lv = sorted(int(t) * 8 + 4 for t in top)
    # A checkerboard is two well-separated light tones. Anything else (a single
    # gray plate, a gradient) is not what this tool is for.
    if not (24 < lv[1] - lv[0] < 95):
        return None
    return lv


def strip(path, tol=10, grow=6):
    im = np.array(Image.open(path).convert("RGBA")).copy()
    a = im[:, :, 3].astype(int)
    c = im[:, :, :3].astype(float)
    g = c.mean(axis=2)
    neutral = ((np.abs(c[:, :, 0] - c[:, :, 1]) < 14) &
               (np.abs(c[:, :, 1] - c[:, :, 2]) < 14) &
               (np.abs(c[:, :, 0] - c[:, :, 2]) < 14))

    lv = _levels(a, g, neutral)
    if lv is None:
        return im, 0, None

    looks = (a > 200) & neutral & ((np.abs(g - lv[0]) <= tol) |
                                   (np.abs(g - lv[1]) <= tol))
    seed = a < 16
    if seed.sum() == 0:
        return im, 0, lv

    lbl, _ = ndimage.label(looks | seed, structure=NEIGH)
    keep = set(np.unique(lbl[seed]))
    keep.discard(0)
    cur = (np.isin(lbl, list(keep)) & looks) | seed

    # Absorb the resampled in-between tones, but only outward from background.
    between = (a > 200) & neutral & (g >= lv[0] - 14) & (g <= lv[1] + 8)
    for _ in range(grow):
        grown = ndimage.binary_dilation(cur, structure=NEIGH) & between
        if not (grown & ~cur).any():
            break
        cur |= grown

    bg = cur & (a > 200)
    im[bg] = [0, 0, 0, 0]
    return im, int(bg.sum()), lv


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+", help="PNG files, or a directory with --scan")
    ap.add_argument("--dry-run", action="store_true", help="report, don't write")
    ap.add_argument("--scan", action="store_true",
                    help="treat paths as directories and only report suspects")
    args = ap.parse_args()

    files = []
    for p in args.paths:
        if args.scan or os.path.isdir(p):
            files += sorted(glob.glob(os.path.join(p, "**", "*.png"), recursive=True))
        else:
            files.append(p)

    total = 0
    for f in files:
        try:
            out, n, lv = strip(f)
        except Exception as exc:                     # unreadable/odd PNG
            print(f"  !! {f}: {exc}", file=sys.stderr)
            continue
        if not n:
            continue
        total += 1
        print(f"{f}: {n} px of checkerboard, levels {lv}"
              + ("  (dry run)" if args.dry_run or args.scan else ""))
        if not (args.dry_run or args.scan):
            Image.fromarray(out).save(f)
    print(f"\n{total} file(s) {'would be' if args.dry_run or args.scan else ''} cleaned.")


if __name__ == "__main__":
    main()
