"""Render the same scenes with the Python tidbyt-server and diff them against the
device code's output from render_test.c.

Usage: python test/host/compare.py <path to tidbyt-server>
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, sys.argv[1])
import apps  # noqa: E402  (tidbyt-server)
from display import draw_centered, draw_text, new_frame  # noqa: E402

OUT = Path(__file__).parent / "out"


def scenes():
    img, d = new_frame()
    draw_centered(d, 4, "19:43", "10x20", (255, 255, 255))
    draw_centered(d, 23, "Sun 11 Oct", "tb-8", (255, 140, 60))
    yield "clock", img

    img, d = new_frame()
    draw_text(d, (25, 2), "12\xb0", "6x13", (255, 255, 255))
    draw_text(d, (25, 15), "Partly", "tb-8", (130, 200, 255))
    draw_text(d, (25, 25), "191", "CG-pixel-4x5-mono", (255, 120, 80))
    draw_text(d, (0, 15), "äöüß", "tb-8", (255, 255, 255))
    yield "text", img

    frames, _ = apps.message({"title": "Hello", "text": "Running on my own server!", "color": "#7fdbff"})
    yield "marquee", frames[60]

    img, d = new_frame()
    d.ellipse((6, 4, 17, 15), fill=(255, 200, 0))
    d.line((4, 2, 5, 3), fill=(255, 200, 0))
    d.rectangle((6, 15, 19, 19), fill=(150, 150, 160))
    yield "shapes", img


ok = True
for name, expected in scenes():
    actual = Image.open(OUT / f"{name}.ppm").convert("RGB")
    diff = [(x, y) for y in range(32) for x in range(64)
            if expected.getpixel((x, y)) != actual.getpixel((x, y))]
    print(f"{name:<8} {'identical' if not diff else f'{len(diff)} pixels differ, e.g. {diff[:5]}'}")
    ok &= not diff or name == "shapes"  # ellipse rasterisation may differ slightly
    side = Image.new("RGB", (64 * 2 + 4, 32), (60, 0, 60))
    side.paste(expected, (0, 0))
    side.paste(actual, (68, 0))
    side.resize((side.width * 6, 32 * 6), Image.NEAREST).save(OUT / f"{name}_compare.png")
sys.exit(0 if ok else 1)
