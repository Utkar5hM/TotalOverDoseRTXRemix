"""Build a labeled contact sheet from the ASI's HUD candidate DDS dumps.

Each tile shows the texture composited over a checkerboard (so alpha-shaped UI
art is visible) with its short hash and dimensions. Output is a single PNG that
can be eyeballed to pick the real HUD/ammo/health/weapon atlases.
"""
import glob
import os
import sys

from PIL import Image, ImageDraw, ImageFont

SRC = sys.argv[1] if len(sys.argv) > 1 else r"rtx-remix\logs\tod-candidate-hud"
OUT = sys.argv[2] if len(sys.argv) > 2 else r"re_docs\hud_candidates_contact_sheet.png"

TILE = 112          # drawn texture area per cell
PAD = 6
LABEL_H = 26
COLS = 8

def checker(size, a=(80, 80, 80), b=(120, 120, 120), step=8):
    img = Image.new("RGB", (size, size), a)
    d = ImageDraw.Draw(img)
    for y in range(0, size, step):
        for x in range(0, size, step):
            if (x // step + y // step) % 2:
                d.rectangle([x, y, x + step, y + step], fill=b)
    return img

def load_rgba(path):
    im = Image.open(path)
    im.load()
    return im.convert("RGBA")

files = sorted(glob.glob(os.path.join(SRC, "*.dds")))
if not files:
    print("no DDS files in", SRC)
    sys.exit(1)

try:
    font = ImageFont.truetype("consola.ttf", 11)
except Exception:
    font = ImageFont.load_default()

cell_w = TILE + PAD * 2
cell_h = TILE + LABEL_H + PAD * 2
rows = (len(files) + COLS - 1) // COLS
sheet = Image.new("RGB", (cell_w * COLS, cell_h * rows), (24, 24, 24))
draw = ImageDraw.Draw(sheet)

ok = 0
for i, path in enumerate(files):
    cx = (i % COLS) * cell_w
    cy = (i // COLS) * cell_h
    name = os.path.basename(path)
    # <role>_<HASH>_<WxH>_<FMT>.dds, where <role> may be one or more words
    # (e.g. hud_candidate, world_candidate, world_unshadowed_candidate). Locate
    # the 16-hex-digit hash field rather than assuming a fixed prefix length.
    parts = name[:-4].split("_")
    hash_idx = next(
        (j for j, p in enumerate(parts)
         if len(p) == 16 and all(c in "0123456789ABCDEFabcdef" for c in p)),
        2,
    )
    short_hash = parts[hash_idx][:8] if hash_idx < len(parts) else "?"
    dims = parts[hash_idx + 1] if hash_idx + 1 < len(parts) else "?"
    try:
        tex = load_rgba(path)
        bg = checker(TILE)
        scale = min(TILE / tex.width, TILE / tex.height)
        nw, nh = max(1, int(tex.width * scale)), max(1, int(tex.height * scale))
        tex = tex.resize((nw, nh), Image.NEAREST)
        bg.paste(tex, ((TILE - nw) // 2, (TILE - nh) // 2), tex)
        sheet.paste(bg, (cx + PAD, cy + PAD))
        ok += 1
    except Exception as exc:
        draw.rectangle([cx + PAD, cy + PAD, cx + PAD + TILE, cy + PAD + TILE], fill=(60, 0, 0))
        draw.text((cx + PAD + 4, cy + PAD + 4), "ERR", fill=(255, 120, 120), font=font)
    draw.text((cx + PAD, cy + PAD + TILE + 4), short_hash, fill=(230, 230, 230), font=font)
    draw.text((cx + PAD, cy + PAD + TILE + 14), dims, fill=(150, 200, 150), font=font)

os.makedirs(os.path.dirname(OUT), exist_ok=True)
sheet.save(OUT)
print(f"wrote {OUT} ({ok}/{len(files)} tiles, {sheet.width}x{sheet.height})")
