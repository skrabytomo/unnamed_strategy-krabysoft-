#!/usr/bin/env python3
"""
Generate placeholder spell icon atlas for all 25 spells.
Output: assets/icons_spells.png  (5 cols × 5 rows, each cell 32×32 = 160×160 total)

School colours:
  Light  = warm gold   (#F5D060)
  Blood  = crimson     (#C42020)
  Death  = teal-black  (#1A6060)
  Nature = forest grn  (#2A8030)
  Forge  = steel blue  (#4060B0)
  Flesh  = sickly grn  (#7AAA30)
"""

import os
import sys
import math
from PIL import Image, ImageDraw

# ── School palette ────────────────────────────────────────────────────────────
SCHOOL = {
    "Light":  {"bg": (60, 45, 5),   "fg": (245, 208, 80),  "rim": (255, 240, 140)},
    "Blood":  {"bg": (55, 5,  5),   "fg": (196, 32,  32),  "rim": (255, 80,  80)},
    "Death":  {"bg": (5,  30, 30),  "fg": (26,  96,  96),  "rim": (60,  200, 180)},
    "Nature": {"bg": (5,  30, 5),   "fg": (42,  128, 48),  "rim": (120, 230, 80)},
    "Forge":  {"bg": (10, 20, 50),  "fg": (64,  96,  176), "rim": (140, 180, 255)},
    "Flesh":  {"bg": (20, 35, 5),   "fg": (122, 170, 48),  "rim": (200, 220, 80)},
}

# ── Spell list in order (matches SpellRegistry.h) ────────────────────────────
SPELLS = [
    # Light
    {"name": "Bless",          "school": "Light",  "shape": "cross",    "label": "BL"},
    {"name": "Smite",          "school": "Light",  "shape": "bolt",     "label": "SM"},
    {"name": "Divine Shield",  "school": "Light",  "shape": "shield",   "label": "DS"},
    {"name": "Radiance",       "school": "Light",  "shape": "starburst","label": "RA"},
    # Blood
    {"name": "Blood Frenzy",   "school": "Blood",  "shape": "flames",   "label": "BF"},
    {"name": "Drain Life",     "school": "Blood",  "shape": "drop",     "label": "DL"},
    {"name": "Enervate",       "school": "Blood",  "shape": "spiral",   "label": "EN"},
    {"name": "Hemorrhage",     "school": "Blood",  "shape": "splash",   "label": "HM"},
    # Death
    {"name": "Curse",          "school": "Death",  "shape": "skull",    "label": "CU"},
    {"name": "Wither",         "school": "Death",  "shape": "decay",    "label": "WI"},
    {"name": "Death Coil",     "school": "Death",  "shape": "coil",     "label": "DC"},
    {"name": "Plague",         "school": "Death",  "shape": "cloud",    "label": "PL"},
    {"name": "Venomous Cloud", "school": "Death",  "shape": "cloud2",   "label": "VC"},
    # Nature
    {"name": "Barkskin",       "school": "Nature", "shape": "leaf",     "label": "BA"},
    {"name": "Entangle",       "school": "Nature", "shape": "vines",    "label": "ET"},
    {"name": "Call Lightning", "school": "Nature", "shape": "bolt",     "label": "CL"},
    {"name": "Regrowth",       "school": "Nature", "shape": "sprout",   "label": "RG"},
    {"name": "Serpent Venom",  "school": "Nature", "shape": "fang",     "label": "SV"},
    # Forge
    {"name": "Reinforce",      "school": "Forge",  "shape": "gear",     "label": "RF"},
    {"name": "Overclock",      "school": "Forge",  "shape": "cog",      "label": "OC"},
    {"name": "Shrapnel",       "school": "Forge",  "shape": "burst",    "label": "SH"},
    {"name": "Hardened Shell", "school": "Forge",  "shape": "shield",   "label": "HS"},
    {"name": "Napalm",         "school": "Forge",  "shape": "flames",   "label": "NP"},
    # Flesh
    {"name": "Fester",         "school": "Flesh",  "shape": "drop",     "label": "FE"},
    {"name": "Mend Flesh",     "school": "Flesh",  "shape": "cross",    "label": "MF"},
    # (25 total — 5 cols × 5 rows)
]
assert len(SPELLS) == 25, f"Expected 25 spells, got {len(SPELLS)}"

CELL = 32   # pixels per icon
COLS = 5
ROWS = 5

# ── Shape primitives (all drawn at 32×32 with a 2px border inset) ─────────────
def draw_icon(spell: dict) -> Image.Image:
    sch  = SCHOOL[spell["school"]]
    img  = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    d    = ImageDraw.Draw(img)
    bg   = sch["bg"]
    fg   = sch["fg"]
    rim  = sch["rim"]
    cx, cy = 16, 16

    # Background circle
    d.ellipse([2, 2, 29, 29], fill=(*bg, 230), outline=(*rim, 180), width=1)

    shape = spell["shape"]

    if shape == "cross":
        d.rectangle([13, 6,  18, 25], fill=(*fg, 230))
        d.rectangle([6,  13, 25, 18], fill=(*fg, 230))
        # rim highlight
        d.rectangle([13, 6,  14, 25], fill=(*rim, 120))
        d.rectangle([6,  13, 25, 14], fill=(*rim, 120))

    elif shape == "bolt":
        pts = [16, 5,  10, 16,  15, 16,  12, 28,  22, 15,  17, 15]
        d.polygon(pts, fill=(*fg, 230))
        d.polygon(pts, outline=(*rim, 200), width=1)

    elif shape == "shield":
        d.polygon([16, 6,  25, 10,  25, 20,  16, 27,  7, 20,  7, 10], fill=(*fg, 220))
        d.polygon([16, 6,  25, 10,  25, 20,  16, 27,  7, 20,  7, 10], outline=(*rim, 200), width=1)
        d.line([16, 8, 16, 24], fill=(*rim, 180), width=1)
        d.line([8, 14, 24, 14], fill=(*rim, 120), width=1)

    elif shape == "starburst":
        for i in range(8):
            angle = math.radians(i * 45)
            x1 = cx + int(4 * math.cos(angle))
            y1 = cy + int(4 * math.sin(angle))
            x2 = cx + int(12 * math.cos(angle))
            y2 = cy + int(12 * math.sin(angle))
            d.line([x1, y1, x2, y2], fill=(*rim, 230), width=2)
        d.ellipse([11, 11, 20, 20], fill=(*fg, 230))

    elif shape == "flames":
        d.ellipse([9, 16, 22, 27], fill=(*fg, 220))
        d.ellipse([11, 10, 20, 22], fill=(*rim, 200))
        d.ellipse([13, 6,  19, 16], fill=(*bg, 180))
        d.ellipse([7, 13, 15, 22], fill=(*fg, 180))
        d.ellipse([17, 13, 25, 22], fill=(*fg, 180))

    elif shape == "drop":
        d.polygon([16, 6,  22, 18,  16, 26,  10, 18], fill=(*fg, 230))
        d.polygon([16, 6,  22, 18,  16, 26,  10, 18], outline=(*rim, 200), width=1)
        d.ellipse([14, 18, 18, 22], fill=(*rim, 160))

    elif shape == "spiral":
        for r in range(9, 3, -1):
            alpha = int(230 * r / 9)
            d.arc([cx-r, cy-r, cx+r, cy+r], start=0, end=270, fill=(*fg, alpha), width=2)

    elif shape == "splash":
        d.ellipse([10, 10, 21, 21], fill=(*fg, 200))
        for angle in [30, 90, 150, 210, 270, 330]:
            rad = math.radians(angle)
            x1 = cx + int(7 * math.cos(rad))
            y1 = cy + int(7 * math.sin(rad))
            x2 = cx + int(13 * math.cos(rad))
            y2 = cy + int(13 * math.sin(rad))
            d.line([x1, y1, x2, y2], fill=(*rim, 220), width=2)

    elif shape == "skull":
        d.ellipse([8, 7, 23, 20], fill=(*fg, 220))
        d.rectangle([10, 19, 21, 25], fill=(*fg, 180))
        d.ellipse([10, 14, 14, 19], fill=(*bg, 230))
        d.ellipse([17, 14, 21, 19], fill=(*bg, 230))
        d.rectangle([12, 21, 14, 25], fill=(*bg, 200))
        d.rectangle([15, 21, 17, 25], fill=(*bg, 200))
        d.rectangle([18, 21, 20, 25], fill=(*bg, 200))
        d.ellipse([8, 7, 23, 20], outline=(*rim, 150), width=1)

    elif shape == "decay":
        d.ellipse([7, 7, 24, 24], fill=(*fg, 160))
        d.arc([7, 7, 24, 24], start=45, end=225, fill=(*rim, 200), width=3)
        d.line([16, 16, 16, 26], fill=(*rim, 180), width=2)
        d.line([12, 22, 20, 22], fill=(*rim, 180), width=1)

    elif shape == "coil":
        for r in range(4, 12):
            angle = math.radians(r * 40)
            x = cx + int(r * math.cos(angle))
            y = cy + int(r * math.sin(angle))
            d.ellipse([x-1, y-1, x+1, y+1], fill=(*fg, 230))
        d.ellipse([14, 14, 17, 17], fill=(*rim, 230))

    elif shape in ("cloud", "cloud2"):
        offset = 2 if shape == "cloud2" else 0
        d.ellipse([6+offset, 13, 18+offset, 24], fill=(*fg, 200))
        d.ellipse([12+offset, 10, 24+offset, 22], fill=(*fg, 200))
        d.ellipse([8+offset, 10, 20+offset, 22], fill=(*rim, 160))
        d.ellipse([5, 18, 14, 26], fill=(*fg, 180))
        d.ellipse([14, 18, 26, 26], fill=(*fg, 180))

    elif shape == "leaf":
        d.polygon([16, 6,  25, 16,  16, 26,  7, 16], fill=(*fg, 230))
        d.line([16, 8, 16, 24], fill=(*rim, 200), width=2)
        d.line([8, 16, 24, 16], fill=(*rim, 120), width=1)
        d.polygon([16, 6,  25, 16,  16, 26,  7, 16], outline=(*rim, 180), width=1)

    elif shape == "vines":
        d.arc([6, 6, 16, 26], start=180, end=0, fill=(*fg, 220), width=2)
        d.arc([16, 6, 26, 26], start=0, end=180, fill=(*fg, 220), width=2)
        for y in [8, 14, 20]:
            d.ellipse([13, y, 17, y+4], fill=(*rim, 180))

    elif shape == "sprout":
        d.line([16, 26, 16, 12], fill=(*fg, 230), width=2)
        d.ellipse([11, 8, 20, 17], fill=(*rim, 220))
        d.ellipse([7, 13, 16, 20], fill=(*fg, 200))
        d.ellipse([16, 13, 25, 20], fill=(*fg, 200))

    elif shape == "fang":
        d.polygon([16, 6,  20, 22,  16, 26,  12, 22], fill=(*fg, 230))
        d.polygon([16, 6,  20, 22,  16, 26,  12, 22], outline=(*rim, 200), width=1)
        d.ellipse([13, 20, 19, 26], fill=(*rim, 200))

    elif shape == "gear":
        d.ellipse([10, 10, 21, 21], fill=(*fg, 220))
        for i in range(8):
            angle = math.radians(i * 45)
            x1 = cx + int(10 * math.cos(angle))
            y1 = cy + int(10 * math.sin(angle))
            x2 = cx + int(14 * math.cos(angle))
            y2 = cy + int(14 * math.sin(angle))
            d.line([x1, y1, x2, y2], fill=(*rim, 220), width=3)
        d.ellipse([12, 12, 19, 19], fill=(*bg, 220))

    elif shape == "cog":
        d.ellipse([9, 9, 22, 22], fill=(*fg, 200))
        for i in range(6):
            angle = math.radians(i * 60)
            x1 = cx + int(11 * math.cos(angle))
            y1 = cy + int(11 * math.sin(angle))
            x2 = cx + int(15 * math.cos(angle))
            y2 = cy + int(15 * math.sin(angle))
            d.line([x1, y1, x2, y2], fill=(*rim, 230), width=2)
        d.ellipse([13, 13, 18, 18], fill=(*bg, 230))

    elif shape == "burst":
        for i in range(12):
            angle = math.radians(i * 30)
            length = 12 if i % 2 == 0 else 8
            x2 = cx + int(length * math.cos(angle))
            y2 = cy + int(length * math.sin(angle))
            d.line([cx, cy, x2, y2], fill=(*fg, 220), width=2)
        d.ellipse([13, 13, 18, 18], fill=(*rim, 230))

    else:
        # fallback: filled circle with school color
        d.ellipse([8, 8, 23, 23], fill=(*fg, 230))

    return img


def main():
    out_dir = os.path.join(os.path.dirname(__file__), "..", "assets")
    os.makedirs(out_dir, exist_ok=True)
    atlas_path = os.path.join(out_dir, "icons_spells.png")

    atlas = Image.new("RGBA", (COLS * CELL, ROWS * CELL), (0, 0, 0, 0))

    print("Generating spell icons:")
    for idx, spell in enumerate(SPELLS):
        col = idx % COLS
        row = idx // COLS
        icon = draw_icon(spell)
        atlas.paste(icon, (col * CELL, row * CELL))
        print(f"  [{idx+1:2d}/25] {spell['name']:20s}  ({spell['school']}, shape={spell['shape']})")

    atlas.save(atlas_path)
    print(f"\nSaved: {atlas_path}  ({COLS*CELL}x{ROWS*CELL}px, {COLS}x{ROWS} cells)")
    print("Atlas layout (col, row):")
    for idx, spell in enumerate(SPELLS):
        print(f"  idx={idx:2d}  col={idx%COLS}  row={idx//COLS}  {spell['name']}")


if __name__ == "__main__":
    main()
