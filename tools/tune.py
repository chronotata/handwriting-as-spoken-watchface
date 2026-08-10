#!/usr/bin/env python3
"""Render every word the watchface can show, and emit the geometry the C needs.

Why images rather than a font
-----------------------------
Drawing text meant asking Pebble to place glyphs, then separately guessing where
they had landed in order to reveal them a slice at a time. The two never agreed
to the pixel and the reveal animation leaked fragments for three iterations.
With images the geometry is exact: the reveal draws a sub-rectangle, so nothing
unrevealed is ever drawn at all.

Canvas heights are UNIFORM PER SIZE FAMILY
------------------------------------------
Every word in a family gets the same canvas height - the union of the family's
ink extents, ascender line to descender line - with the baseline at the same
offset in every one. Widths stay tight to each word's own ink.

That means the layout's row pitch is constant, so a word's neighbours never
move when it is replaced by a different word: "twenty-" holds still as "three"
becomes "four", and baselines land on the same lines all day, like ruled paper.

It costs the property the tight-canvas version had - that every visual ink gap
was exactly ROW_GAP. Now ROW_GAP is only the MINIMUM gap, reached when a full
descender sits above a full ascender; elsewhere the gap opens up by whatever
ink the two words do not use. That trade was made deliberately: even baselines
matter more than even ink gaps, both on screen and as a substrate to draw real
handwriting over in version 2.

The padding also exists to be drawn into. A tight "one" was 16px tall and left
nowhere to put a flourish; on the family box it is 47px with room above and
below the baseline.

    python3 tools/tune.py       # requires Pillow
"""

import json
import re
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Pillow is required:  "
             "pip install Pillow --break-system-packages")

ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "src" / "c" / "config.h"
GEOM = ROOT / "src" / "c" / "geometry.h"
TESTGEN = ROOT / "tools" / "test" / "generated.h"
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

# SUNDAY FIRST, to match struct tm's tm_wday. handwritten.c indexes
# kWeekdays[] with tm_wday directly, so this order is load-bearing: rotating
# it silently shifts every weekday by a day rather than failing to build.
WEEKDAYS = ["Sun.", "Mon.", "Tue.", "Wed.", "Thu.", "Fri.", "Sat."]


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
    # Appended LAST on purpose. Resource ids are this list's index + 1, so
    # adding anywhere earlier would renumber every word after it and churn
    # geometry.h and package.json for no reason.
    out += [("W_DOW_" + d[:3].upper(), "DATE", d) for d in WEEKDAYS]
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


def read_layout():
    """The layout constants needed for the vertical budget check."""
    text = CONFIG.read_text()
    out = {}
    for key in ("ROW_GAP", "REL_TOP", "DATE_BASELINE", "SCREEN_H"):
        m = re.search(r"^#define\s+%s\s+(-?\d+)" % key, text, re.M)
        if not m:
            sys.exit("could not find %s in config.h" % key)
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


def family_boxes(rendered):
    """-> {family: (max ascent, max descent)} over every word in that family.

    Font metrics are deliberately NOT used here. A script face's declared
    ascent/descent are far larger than any glyph it actually draws, and padding
    to them would waste 20-odd pixels a row that this layout cannot spare. The
    union of real ink extents is the tightest box that still holds every word.
    """
    box = {}
    for _name, fam, _text, crop, baseline in rendered:
        asc, desc = baseline, crop.height - baseline
        a, d = box.get(fam, (0, 0))
        box[fam] = (max(a, asc), max(d, desc))
    return box


def pad_to_box(crop, baseline, box):
    """Place a tight crop on its family box, aligned by BASELINE - which is the
    whole point: every word in a family then shares one baseline offset, so
    swapping one for another moves nothing."""
    asc, desc = box
    out = Image.new("L", (crop.width, asc + desc), 0)
    out.paste(crop, (0, asc - baseline))
    return out


def check_fit(rendered, box, layout):
    """Does the tallest possible phrase still clear the screen and the date?

    The worst case is a split minute word: four TIME rows stacked
    ("twenty-" / "eight" / "past" / "midnight" at :28). This checks the
    vertical budget only - pure arithmetic over the boxes and the config
    constants. It is a guard rail, NOT verification: the layout itself is
    verified by compiling handwritten.c against a Pebble stub and sweeping all
    1440 minutes, per CONTEXT.md section 5.
    """
    A, D = box["TIME"]
    H, G = A + D, layout["ROW_GAP"]
    rel_top = layout["REL_TOP"]
    date_ink_top = layout["DATE_BASELINE"] - box["DATE"][0]

    asc = {t: b for _n, f, t, _c, b in rendered if f == "TIME"}

    # Row 0 of a split phrase is always "twenty-"; its unused ascent is the
    # only slack at the top of the screen.
    top_canvas = rel_top - 2 * (H + G)
    ink_top = top_canvas + (A - asc["twenty-"])
    # The hour row's descent is fully used by "midnight", so no slack below.
    ink_bottom = rel_top + 2 * H + G

    problems = []
    if ink_top < 0:
        problems.append("top row is clipped by %dpx (lower REL_TOP or ROW_GAP)"
                        % -ink_top)
    if ink_bottom > date_ink_top - 1:
        problems.append("hour row collides with the date by %dpx "
                        "(raise DATE_BASELINE, or lower REL_TOP/ROW_GAP)"
                        % (ink_bottom - date_ink_top + 1))

    print("\nvertical budget (worst case, :28 'twenty-eight past midnight')")
    print("  TIME box      %dpx  (ascent %d / descent %d)" % (H, A, D))
    print("  phrase ink    %d..%d" % (ink_top, ink_bottom))
    print("  date ink top  %d          clearance %dpx"
          % (date_ink_top, date_ink_top - ink_bottom))
    print("  min visible gap between rows: %dpx (= ROW_GAP)" % G)
    if problems:
        sys.exit("\nDOES NOT FIT:\n  - " + "\n  - ".join(problems))


def write_test_header(rendered, box):
    """Scaffolding for tools/test/harness.c. Never compiled into the watchface.

    Two things the SDK or the watchface would otherwise provide:

      RESOURCE_ID_*  - normally generated by the Pebble build from
                       package.json; the harness builds without the SDK.
      WORD_INK[]     - each word's REAL ink extents. The shipping WORDS[]
                       table carries the family box instead, so a canvas is
                       routinely taller than the letters inside it and
                       bounds-checking against it would be meaningless. The
                       harness needs the ink to know what is actually visible.
    """
    if not TESTGEN.parent.is_dir():
        return
    out = ["/* GENERATED by tools/tune.py - do not edit. */",
           "/* Test scaffolding only. Not compiled into the watchface. */",
           "", "#pragma once", "#include <stdint.h>", ""]
    for i, (name, _fam, _text, _crop, _base) in enumerate(rendered):
        out.append("#define RESOURCE_ID_%s %d" % (name.upper(), i + 1))
    out += ["", "typedef struct { uint8_t asc, desc; } WordInk;", "",
            "/* Ink above and below the baseline, per word. */",
            "static const WordInk WORD_INK[] = {"]
    for name, fam, text, crop, baseline in rendered:
        out.append("  {%d, %d},%s/* %s */"
                   % (baseline, crop.height - baseline,
                      " " * max(1, 10 - len(str(baseline))), text))
    out += ["};", ""]
    TESTGEN.write_text("\n".join(out))


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
    layout = read_layout()
    IMGDIR.mkdir(parents=True, exist_ok=True)
    EXPORT.mkdir(parents=True, exist_ok=True)

    # Pass 1: render tight, so the family boxes can be measured.
    rendered = []
    for name, fam, text in words():
        crop, baseline = render_word(text, sizes[fam])
        rendered.append((name, fam, text, crop, baseline))

    box = family_boxes(rendered)
    check_fit(rendered, box, layout)

    # Pass 2: pad every word onto its family box and write it out.
    rows, media, total = [], [], 0
    for name, fam, text, crop, baseline in rendered:
        img = pad_to_box(crop, baseline, box[fam])
        stem = name.lower()
        path = IMGDIR / ("%s.png" % stem)
        to_palette(img).save(path, transparency=0)
        total += path.stat().st_size
        img.save(EXPORT / ("%s.png" % stem))   # template to draw over for v2

        rows.append((name, img.width, img.height, box[fam][0], text))
        media.append({"type": "bitmap", "name": name.upper(),
                      "file": "images/%s.png" % stem,
                      "memoryFormat": "4BitPalette",
                      "storageFormat": "pbi"})

    out = ["/* GENERATED by tools/tune.py - do not edit. */",
           "/* Canvas HEIGHT and BASELINE are uniform within a size family, so */",
           "/* swapping one word for another moves nothing around it. Widths   */",
           "/* stay tight to each word's own ink. */",
           "", "#pragma once", "#include <stdint.h>", ""]
    out.append("/* Marks a geometry.h new enough to contain the weekday words.")
    out.append("   handwritten.c #errors without it, so a stale file cannot be")
    out.append("   built against silently - the same trick as TIME_BOX_H. */")
    out.append("#define GEOMETRY_HAS_WEEKDAYS 1")
    out.append("")
    out.append("/* Family boxes: ascent + descent, and the shared baseline offset. */")
    for fam in ("TIME", "SOLO", "MINS", "DATE", "ORD"):
        a, d = box[fam]
        out.append("#define %s_BOX_H %d" % (fam, a + d))
        out.append("#define %s_BOX_BASE %d" % (fam, a))
    out.append("")
    out.append("/* Ink ascent of \"twenty-\", the top row of the tallest phrase. The")
    out.append("   difference from TIME_BOX_BASE is the only slack at the top of the")
    out.append("   screen, so config.h's budget assertion needs it. */")
    out.append("#define SPLIT_HEAD_ASC %d"
               % next(b for _n, f, t, _c, b in rendered
                      if f == "TIME" and t == "twenty-"))
    out += ["",
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

    write_test_header(rendered, box)

    widest = max(rows, key=lambda r: r[1])
    print("\nwrote %d images, %.1f kB -> resources/images/" % (len(rows), total / 1024))
    print("wrote src/c/geometry.h and package.json")
    print("wrote %d templates -> handwriting-templates/  (draw over these for v2)"
          % len(rows))
    print("widest  %-12r %3dpx" % (widest[4], widest[1]))
    print("family boxes  " + "  ".join(
        "%s %dpx/base %d" % (f, box[f][0] + box[f][1], box[f][0])
        for f in ("TIME", "SOLO", "MINS", "DATE", "ORD")))
    print("\nnext:  pebble build && pebble install --emulator emery")


if __name__ == "__main__":
    main()
