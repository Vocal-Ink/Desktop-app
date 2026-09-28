#!/usr/bin/env python3
"""Generates the Vocal Ink app and tray icons in every format the packages need.

The mark is the same thing the app draws while it talks: one calligraphic
brush stroke that swells like a voice, violet ink on midnight paper.

Requires Pillow (pip install pillow). Run from the repository root:
    python3 tools/make_icons.py
Outputs resources/icons/app-<size>.png, VocalInk.ico, VocalInk.icns,
tray.png and tray-live.png.
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

OUT = Path(__file__).resolve().parent.parent / "resources" / "icons"
S = 1024  # master size

PAPER_TOP = (34, 26, 59)      # #221A3B
PAPER_BOTTOM = (17, 12, 31)   # #110C1F
INK = (140, 82, 255)          # #8c52ff, the signature violet
WET = (91, 43, 217)           # #5B2BD9
SHEEN = (255, 122, 217)       # #FF7AD9
LIVE = (255, 79, 109)         # #FF4F6D


def lerp(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))


def smoothstep(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def stroke_polygon(size, x0, x1, cy, max_half, base_half, rise=0.0):
    """A tapered brush stroke whose thickness follows a short spoken phrase.

    Like a broad nib held at an angle, the upper edge carries most of the
    swell and the lower edge stays calmer, and the line climbs slightly as
    handwriting does."""
    pts_top, pts_bottom = [], []
    steps = 240

    def env(t):
        # Three syllables, the middle one stressed.
        return (0.55 * math.exp(-((t - 0.24) / 0.10) ** 2)
                + 1.00 * math.exp(-((t - 0.52) / 0.12) ** 2)
                + 0.42 * math.exp(-((t - 0.79) / 0.08) ** 2))

    for i in range(steps + 1):
        t = i / steps
        x = x0 + (x1 - x0) * t
        taper = smoothstep(0.0, 0.12, t) * (1 - 0.75 * smoothstep(0.84, 1.0, t))
        y = cy + (0.5 - t) * rise
        top = (base_half + env(t) * max_half) * taper
        bottom = (base_half + env(max(0.0, t - 0.035)) * max_half * 0.55) * taper
        pts_top.append((x, y - top))
        pts_bottom.append((x, y + bottom))
    return pts_top + pts_bottom[::-1]


def gradient(size, colors, horizontal=True):
    img = Image.new("RGBA", (size, size))
    px = img.load()
    for y in range(size):
        for x in range(size):
            t = (x if horizontal else y) / (size - 1)
            if t < 0.5:
                c = lerp(colors[0], colors[1], t / 0.5)
            else:
                c = lerp(colors[1], colors[2], (t - 0.5) / 0.5)
            px[x, y] = c + (255,)
    return img


def master() -> Image.Image:
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # Midnight paper: a rounded square with a quiet vertical fade.
    paper = gradient(S, [PAPER_TOP, lerp(PAPER_TOP, PAPER_BOTTOM, 0.5), PAPER_BOTTOM], horizontal=False)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle((40, 40, S - 40, S - 40), radius=232, fill=255)
    img.paste(paper, (0, 0), mask)

    # A soft violet bloom where the ink sits.
    bloom = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(bloom).ellipse((180, 330, 860, 700), fill=INK + (70,))
    bloom = bloom.filter(ImageFilter.GaussianBlur(90))
    img.alpha_composite(Image.composite(bloom, Image.new("RGBA", (S, S)), mask))

    # The stroke, drawn 2x and scaled down for clean edges.
    big = S * 2
    stroke_mask = Image.new("L", (big, big), 0)
    poly = stroke_polygon(big, 0.16 * big, 0.80 * big, 0.56 * big, 0.21 * big, 0.014 * big, rise=0.10 * big)
    ImageDraw.Draw(stroke_mask).polygon(poly, fill=255)
    stroke_mask = stroke_mask.resize((S, S), Image.LANCZOS)
    ink = gradient(S, [WET, INK, SHEEN])
    img.alpha_composite(Image.composite(ink, Image.new("RGBA", (S, S)), stroke_mask))

    # One drop of ink leaving the nib.
    d = ImageDraw.Draw(img)
    d.ellipse((836, 470, 880, 514), fill=SHEEN + (255,))
    return img


def tray(color) -> Image.Image:
    size = 64
    big = size * 8
    m = Image.new("L", (big, big), 0)
    poly = stroke_polygon(big, 0.04 * big, 0.96 * big, 0.56 * big, 0.36 * big, 0.035 * big, rise=0.12 * big)
    ImageDraw.Draw(m).polygon(poly, fill=255)
    m = m.resize((size, size), Image.LANCZOS)
    out = Image.new("RGBA", (size, size), color + (0,))
    out.putalpha(m)
    return out


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    m = master()
    for size in (16, 24, 32, 48, 64, 128, 256, 512, 1024):
        m.resize((size, size), Image.LANCZOS).save(OUT / f"app-{size}.png")
    m.save(OUT / "VocalInk.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    m.save(OUT / "VocalInk.icns")
    tray((169, 112, 255)).save(OUT / "tray.png")
    tray(LIVE).save(OUT / "tray-live.png")
    print("icons written to", OUT)


if __name__ == "__main__":
    main()
