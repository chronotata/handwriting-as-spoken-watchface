#!/usr/bin/env python3
"""Render every word the watchface can show, and emit the geometry the C needs.

Why images rather than a font
-----------------------------
Drawing text meant asking Pebble to place glyphs, then separately guessing where
they had landed in order to reveal them a slice at a time. The two never agreed
to the pixel and the reveal animation leaked fragments for three iterations.
With images the geometry is exact: the reveal draws a sub-rectangle, so nothing
unrevealed is ever drawn at all.

Canvases are TIGHT around the ink and the layout stacks them with a constant
gap, so every visual gap on screen is identical - and any one of them can be
opened later by adding blank rows to that word's PNG. Padding an image IS the
spacing control, which matters most for version 2 where these are replaced with
real handwriting.

    python tools/tune.py        # requires Pillow
"""

import json
import re
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Pillow is required:  pip install Pillow")

ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "src" / "c" / "config.h"
GEOM = ROOT / "src" / "c" / "geometry.h"
PACKAGE = ROOT / "package.json"
TTF = ROOT / "resources" / "fonts" / "watchface.ttf"
IMGDIR = ROOT / "resources" / "images"
EXPORT = ROOT / "handwriting-templates"

SS = 4          # supersample factor, for antialiasing
LEVELS = 16     # 4-bit palette
PAD = 80

# Contrast stretch applied after downsampling.
#
# Pebble has only four levels per channel, so a normal antialiased edge - which
# a desktop renders across dozens of shades - lands on two or three chunky greys
# instead. Half of every stroke ended up grey and the words read as grey rather
# than white, even though the cores were correct.
#
# Coverage below INK_LO becomes background, above INK_HI becomes full ink, and
# the narrow band between keeps a one-pixel softened edge. At 70/150 about 84%
# of each stroke is full ink.
#
# Lower INK_LO and raise INK_HI for softer, greyer edges; narrow the gap toward
# a hard threshold for crisper, more aliased ones.
INK_LO = 70
INK_HI = 150

ONES = ["one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
        "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen",
        "sixteen", "seventeen", "eighteen", "nineteen"]
MONTHS = ["Jan.", "Feb.", "Mar.", "Apr.", "May", "Jun.",
          "Jul.", "Aug.", "Sep.", "Oct.", "Nov.", "Dec."]


def words():
    """(C enum name, size family, text). '|' means half a space."""
    out = [("W_" + w.upper(), "TIME", w) for w in ONES]
    out.append(("W_TWENTY", "TIME", "twenty"))
    out.append(("W_TWENTYDASH", "TIME", "twenty-"))
    for w in ("quarter", "half", "past", "to", "midnight", "midday", "noon",
              "the", "witching", "hour"):
        out.append(("W_" + w.upper(), "TIME", w))
    out.append(("W_OCLOCK", "TIME", "o'|clock"))
    out.append(("W_MINUTE", "MINS", "minute"))
    out.append(("W_MINUTES", "MINS", "minutes"))
    out.append(("W_SOLO_MIDNIGHT", "SOLO", "midnight"))
    out.append(("W_SOLO_MIDDAY", "SOLO", "midday"))
    out += [("W_D%d" % d, "DATE", str(d)) for d in range(10)]
    out += [("W_MON%d" % (i + 1), "DATE", m) for i, m in enumerate(MONTHS)]
    out += [("W_" + o.upper(), "ORD", o) for o in ("st", "nd", "rd", "th")]
    return out


def read_sizes():
    text = CONFIG.read_text()
    out = {}
    for key in ("TIME", "SOLO", "MINS", "DATE", "ORD"):
        m = re.search(r"^#define\s+FONT_SIZE_%s\s+(\d+)" % key, text, re.M)
        if not m:
            sys.exit("could not find FONT_SIZE_%s in config.h" % key)
        out[key] = int(m.group(1))
    return out


def half_space(font):
    return max(1, round((font.getbbox("x x")[2] - font.getbbox("xx")[2]) / 2))


def draw_word(d, xy, text, font, scale, base_font):
    """'|' inserts half a space - "o' clock" reads better than "o'clock",
    but a full space is too wide."""
    if "|" not in text:
        d.text(xy, text, font=font, fill=255, anchor="ls")
        return
    a, b = text.split("|")
    x, y = xy
    d.text((x, y), a, font=font, fill=255, anchor="ls")
    adv = (base_font.getbbox(a)[2] - base_font.getbbox(a)[0]
           + half_space(base_font))
    d.text((x + adv * scale, y), b, font=font, fill=255, anchor="ls")


def render_word(text, size):
    """-> (image cropped tight to the ink, baseline offset from its top)"""
    big = ImageFont.truetype(str(TTF), size * SS)
    small = ImageFont.truetype(str(TTF), size)
    img = Image.new("L", ((PAD * 2 + 320) * SS, (PAD * 2 + 140) * SS), 0)
    draw_word(ImageDraw.Draw(img), (PAD * SS, (PAD + 70) * SS), text, big, SS,
              small)
    img = img.resize((PAD * 2 + 320, PAD * 2 + 140), Image.LANCZOS)
    img = img.point([0 if v < INK_LO else
                     (255 if v > INK_HI else
                      int(255 * (v - INK_LO) / (INK_HI - INK_LO)))
                     for v in range(256)])
    box = img.point(lambda v: 255 if v > 12 else 0).getbbox()
    if box is None:
        sys.exit("nothing rendered for %r" % text)
    return img.crop(box), (PAD + 70) - box[1]


# Pebble has 64 colours - two bits per channel - so the only greys that exist
# are 0, 85, 170 and 255. A 16-step GREY palette therefore collapses to four
# distinct colours during conversion, and the SDK re-indexes the pixels to
# match. That silently destroys the index->intensity mapping the watchface
# relies on, and every word renders as near-black.
#
# So the source palette uses 16 colours that stay distinct after quantisation,
# encoding the level in the red and green channels. What they look like does
# not matter: the C overwrites the whole palette at load with a paper-to-ink
# ramp. Only distinctness matters.
CH = [0, 85, 170, 255]


def level_colour(i):
    return CH[i & 3], CH[(i >> 2) & 3], 0


def to_palette(img):
    """Quantise to LEVELS levels. Index 0 is transparent; the C rewrites the
    palette at load so ink and paper colours stay configurable."""
    q = img.point(lambda v: min(LEVELS - 1, v * LEVELS // 256))
    out = Image.new("P", q.size)
    out.putdata(list(q.getdata()))
    pal = []
    for i in range(LEVELS):
        pal += list(level_colour(i))
    pal += [0, 0, 0] * (256 - LEVELS)
    out.putpalette(pal)
    return out


def main():
    if not TTF.exists():
        sys.exit("missing font: %s" % TTF)
    sizes = read_sizes()
    IMGDIR.mkdir(parents=True, exist_ok=True)
    EXPORT.mkdir(parents=True, exist_ok=True)

    rows, media, total = [], [], 0
    for name, fam, text in words():
        crop, baseline = render_word(text, sizes[fam])
        stem = name.lower()
        path = IMGDIR / ("%s.png" % stem)
        to_palette(crop).save(path, transparency=0)
        total += path.stat().st_size
        crop.save(EXPORT / ("%s.png" % stem))   # template to draw over for v2

        rows.append((name, crop.width, crop.height, baseline, text))
        media.append({"type": "bitmap", "name": name.upper(),
                      "file": "images/%s.png" % stem,
                      "memoryFormat": "4BitPalette",
                      "storageFormat": "pbi"})

    out = ["/* GENERATED by tools/tune.py - do not edit. */",
           "/* Canvases are tight around the ink; the layout stacks them with */",
           "/* a constant gap, so padding a PNG opens that row's spacing. */",
           "", "#pragma once", "#include <stdint.h>", "",
           "typedef struct {",
           "  uint32_t res;   /* resource id            */",
           "  uint8_t w, h;   /* canvas size in pixels  */",
           "  uint8_t base;   /* canvas top -> baseline */",
           "} WordGeom;", "", "typedef enum {"]
    for name, w, h, base, text in rows:
        out.append("  %s,%s/* %s */" % (name, " " * max(1, 18 - len(name)), text))
    out += ["  W_COUNT", "} WordId;", "",
            "static const WordGeom WORDS[W_COUNT] = {"]
    for name, w, h, base, text in rows:
        out.append("  {RESOURCE_ID_%s, %d, %d, %d}," % (name.upper(), w, h, base))
    out += ["};", ""]
    GEOM.write_text("\n".join(out))

    pkg = json.loads(PACKAGE.read_text())
    pkg["pebble"]["resources"]["media"] = media
    PACKAGE.write_text(json.dumps(pkg, indent=2) + "\n")

    widest = max(rows, key=lambda r: r[1])
    tallest = max(rows, key=lambda r: r[2])
    print("wrote %d images, %.1f kB -> resources/images/" % (len(rows), total / 1024))
    print("wrote src/c/geometry.h and package.json")
    print("wrote %d templates -> handwriting-templates/  (draw over these for v2)"
          % len(rows))
    print("widest  %-12r %3dpx" % (widest[4], widest[1]))
    print("tallest %-12r %3dpx" % (tallest[4], tallest[2]))
    print("\nnext:  pebble build && pebble install --emulator emery")


if __name__ == "__main__":
    main()
