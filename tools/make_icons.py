#!/usr/bin/env python3
"""Generates the Vocal Ink app icon in every format the packages need.

Requires Pillow (pip install pillow). Run from the repository root:
    python3 tools/make_icons.py
Outputs resources/icons/app-<size>.png, VocalInk.ico and VocalInk.icns.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

OUT = Path(__file__).resolve().parent.parent / "resources" / "icons"
S = 1024  # master size


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def master() -> Image.Image:
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # Rounded-square background with a diagonal ink gradient.
    top, bottom = (104, 76, 255), (30, 184, 196)
    grad = Image.new("RGBA", (S, S))
    gp = grad.load()
    for y in range(S):
        for x in range(S):
            t = (x * 0.35 + y * 0.65) / S
            gp[x, y] = lerp(top, bottom, min(1.0, t)) + (255,)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle((40, 40, S - 40, S - 40), radius=220, fill=255)
    img.paste(grad, (0, 0), mask)

    d = ImageDraw.Draw(img)
    white = (255, 255, 255, 255)

    # Speech bubble.
    bubble = (190, 220, 834, 700)
    d.rounded_rectangle(bubble, radius=150, fill=white)
    d.polygon([(330, 660), (300, 850), (500, 690)], fill=white)

    # Sound-wave bars inside the bubble (the voice).
    ink = (78, 60, 210, 255)
    heights = [110, 210, 300, 210, 110]
    cx, cy, w, gap = S // 2, 460, 56, 38
    x0 = cx - (len(heights) * w + (len(heights) - 1) * gap) // 2
    for i, h in enumerate(heights):
        x = x0 + i * (w + gap)
        d.rounded_rectangle((x, cy - h // 2, x + w, cy + h // 2), radius=w // 2, fill=ink)

    # Soft shadow under the bubble for depth.
    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle((200, 250, 844, 730), radius=150, fill=(0, 0, 0, 70))
    shadow = shadow.filter(ImageFilter.GaussianBlur(24))
    base = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    base.paste(grad, (0, 0), mask)
    base.alpha_composite(shadow)
    base.alpha_composite(Image.composite(img, Image.new("RGBA", (S, S)), img.split()[3]))
    return base


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    m = master()
    for size in (16, 24, 32, 48, 64, 128, 256, 512, 1024):
        m.resize((size, size), Image.LANCZOS).save(OUT / f"app-{size}.png")
    m.save(OUT / "VocalInk.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    m.save(OUT / "VocalInk.icns")
    print("icons written to", OUT)


if __name__ == "__main__":
    main()
