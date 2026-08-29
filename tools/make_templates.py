#!/usr/bin/env python3
"""
Build the handwriting templates for the Supernote Nomad.

    python3 tools/make_templates.py TIME
    python3 tools/make_templates.py            # every family

Writes handwriting-templates/out/handwritten_<FAMILY>.pdf plus a JSON
manifest naming the exact pixel box every word is to be drawn in.


WHY THE PAGE IS THE SHAPE IT IS
-------------------------------
The Nomad exports annotations as a SEPARATE transparent image laid over the
page - RGB ink plus an 8-bit alpha soft mask - and that image is 1404x1872
whatever the page's physical size, because it is the screen buffer. So:

  * the ink comes back clean. The template underneath is untouched vector
    content and simply is not in the annotation layer, which means the
    guidelines and the printed reference word cannot contaminate the scan
    and do not have to be filtered out of it.

  * 1404x1872 is the entire resolution budget for a page, and no page size
    or export setting buys more of it.

Both of those point the same way: make the page 3:4 and lay it out in
1404x1872 pixels, so one template pixel is one ink pixel and the box a word
was drawn in is the box the pipeline crops - no registration, no rescaling,
no rounding. The page is 612x816pt, which is the size the sample export
used, so it is known to survive the round trip.


SCALE, AND WHY IT DIFFERS PER FAMILY
------------------------------------
A word drawn at scale S is reduced by S on the way back, and everything
shrinks with it - including the thickness of the pen line. So a line drawn
at 12.5px on the tablet arrives at the watch as 2.5px.

The font being replaced draws at 2.83px, and its x-height is 18px, so the
ratio that has to survive is INK_RATIO below. Each family is then scaled so
that ONE pen setting is right for all of them - which is the whole point,
since nobody wants to change pens between families:

    pen (px)  =  INK_RATIO * x-height * S       ->   S = PEN_PX / (INK_RATIO * x)

PEN_PX is not a setting anyone types in. It is the measured thickness a pen
has to have for the arithmetic above to come out right, and it is printed on
every page as a reminder of what the templates were built for.

Both numbers were first estimated and then MEASURED off a real drawn page:
the estimate said 0.19 and 17px, the measurement says 0.157 and 14px. The
scales are unchanged by the correction, which is the useful part - the
estimate was wrong in a way that only ever showed up as advice to use a
heavier pen than necessary.
"""

import json
import re
import struct
import sys
import zlib
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Pillow is required:  "
             "pip install Pillow --break-system-packages")

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "handwriting-templates"
OUT = SRC / "out"
GEOM = ROOT / "src" / "c" / "geometry.h"
INKINDEX = SRC / "ink" / "index.json"

# --- the page, in ink pixels -------------------------------------------
PAGE_W, PAGE_H = 1404, 1872
PAGE_PT = (612, 816)          # 3:4, as used by the sample Nomad export
MARGIN = 60

# --- families ----------------------------------------------------------
#
# The canvas heights and baselines are READ FROM geometry.h, never written
# down here. They used to be a hardcoded table keyed by the template PNG's
# height, and it went stale exactly as you would expect: the DATE box grew
# from 28px to 29, this table did not, and every date word was drawn against
# a box a pixel shorter than the one the watchface reserves for it. The
# weekdays - the only DATE words whose ink reaches the top of the box -
# sprouted a repeated row underneath on the real screen.
#
# One number is genuinely not in geometry.h: the x-height, which is a
# property of the typeface rather than of the canvas. It stays here, keyed by
# NAME rather than by height so that it cannot silently attach itself to the
# wrong family when a box changes size.
X_HEIGHT = {"TIME": 18, "SOLO": 21, "HEDGE": 11,
            "MINS": 11, "DATE": 10, "ORD": 6}


def family_boxes():
    """{family: (canvas height, baseline from top)} out of geometry.h."""
    if not GEOM.exists():
        sys.exit("src/c/geometry.h is missing - run tools/tune.py first")
    text = GEOM.read_text()
    out = {}
    for fam in X_HEIGHT:
        h = re.search(r"^#define %s_BOX_H\s+(\d+)" % fam, text, re.M)
        b = re.search(r"^#define %s_BOX_BASE\s+(\d+)" % fam, text, re.M)
        if not h or not b:
            sys.exit("geometry.h has no %s box - run tools/tune.py" % fam)
        out[fam] = (int(h.group(1)), int(b.group(1)))
    heights = [v[0] for v in out.values()]
    if len(set(heights)) != len(heights):
        sys.exit("two families now share a canvas height, so a template PNG "
                 "no longer says which family it belongs to: %r" % (out,))
    return out

# How wide the box to write in should be, per family.
#
# TIME and SOLO get the whole screen budget - 181px, which is what a 200px
# screen leaves after a 10px left margin and the 9px right margin
# "midnight" already lives with - because their words really do run to the
# edge and it is worth seeing that.
#
# The rest are pieces of a composed line rather than a line on their own, so
# the screen budget says nothing useful about any one of them. They get
# their own widest word plus a third again, which is room to be generous
# without being an invitation to fill it.
FULL_WIDTH = {"TIME", "SOLO"}
MAX_WORD_W = 181
HEADROOM = 1.35

# Measured, not guessed: a drawn TIME page came back at 12.5px, which lands
# at 2.5px on the watch against the font's 2.83px. 14px is what would have
# matched exactly. About 1.2mm on the Nomad's 300dpi screen.
PEN_PX = 14
INK_RATIO = 2.83 / 18.0       # the font's stroke width over its x-height

# --- ink ---------------------------------------------------------------
BLACK = 0
GUIDE_BASE = 90               # the baseline - the one to sit letters on
GUIDE = 150                   # ascender and descender limits
GUIDE_MEAN = 190              # the mean line, a hint rather than a rule
GUIDE_FAINT = 205             # the box outline
LABEL = 90


# --- what the page IS, written on the page ------------------------------
#
# An exported file can be renamed, split, merged or drawn on out of order,
# and the page number alone does not say which family it belongs to - page 1
# of TIME and page 1 of SOLO are both "page 1". Reading the family off the
# filename works right up until someone calls the export "test2.pdf".
#
# So each page carries its own identity as a row of squares: a start square,
# the family, the page number, and a parity bit. It survives the round trip
# because the Nomad keeps the page as the background under the annotation
# layer, and a solid 20px square is not something JPEG can damage.
FAMILY_ORDER = ["TIME", "SOLO", "HEDGE", "MINS", "DATE", "ORD"]
MARK_CELL = 20
MARK_Y = 10


def mark_cells(fam, page):
    """-> [bool] * 12, most significant first: start, family, page, parity."""
    bits = [True]
    bits += [(FAMILY_ORDER.index(fam) >> b) & 1 == 1 for b in (2, 1, 0)]
    bits += [(page >> b) & 1 == 1 for b in (6, 5, 4, 3, 2, 1, 0)]
    bits += [sum(bits) % 2 == 1]
    return bits


def draw_mark(d, fam, page):
    bits = mark_cells(fam, page)
    x = PAGE_W - MARGIN - len(bits) * MARK_CELL
    for i, on in enumerate(bits):
        if on:
            d.rectangle([x + i * MARK_CELL, MARK_Y,
                         x + i * MARK_CELL + MARK_CELL - 4,
                         MARK_Y + MARK_CELL - 4], fill=BLACK)


def write_pdf(path, pages, size_pt):
    """One full-page greyscale image per page, Flate-compressed.

    Written by hand rather than through Pillow or a PDF library, because
    neither gives what is wanted without a dependency this project does not
    have. Pillow re-encodes greyscale as JPEG, which puts ringing around
    every guideline - and a guideline is a thing to aim a pen at, so it
    should be a clean edge. Its lossless path is paletted ASCII hex, which
    came out at eighty times the size. Nothing else here needs a PDF
    library, and one page holding one image is a small enough format to
    write out longhand.
    """
    w_pt, h_pt = size_pt
    objs = [None]                       # 1-based; objs[0] is unused

    def add(body):
        objs.append(body)
        return len(objs) - 1        # objs[0] is the unused free entry

    add(b"")                            # 1: catalog, filled in below
    add(b"")                            # 2: page tree
    kids = []
    for page in pages:
        raw = zlib.compress(page.convert("L").tobytes(), 9)
        img = add(b"<</Type/XObject/Subtype/Image/Width %d/Height %d"
                  b"/ColorSpace/DeviceGray/BitsPerComponent 8"
                  b"/Filter/FlateDecode/Length %d>>\nstream\n%s\nendstream"
                  % (page.width, page.height, len(raw), raw))
        content = ("q %f 0 0 %f 0 0 cm /Im0 Do Q" % (w_pt, h_pt)).encode()
        cont = add(b"<</Length %d>>\nstream\n%s\nendstream"
                   % (len(content), content))
        pg = add(b"<</Type/Page/Parent 2 0 R/MediaBox[0 0 %f %f]"
                 b"/Resources<</XObject<</Im0 %d 0 R>>>>/Contents %d 0 R>>"
                 % (w_pt, h_pt, img, cont))
        kids.append(pg)

    objs[1] = b"<</Type/Catalog/Pages 2 0 R>>"
    objs[2] = (b"<</Type/Pages/Kids[" +
               b" ".join(b"%d 0 R" % k for k in kids) +
               b"]/Count %d>>" % len(kids))

    out = bytearray(b"%PDF-1.4\n")
    offsets = [0]
    for i in range(1, len(objs)):
        offsets.append(len(out))
        out += b"%d 0 obj\n" % i + objs[i] + b"\nendobj\n"
    xref = len(out)
    out += b"xref\n0 %d\n" % len(objs)
    out += b"0000000000 65535 f \n"
    for i in range(1, len(objs)):
        out += b"%010d 00000 n \n" % offsets[i]
    out += (b"trailer\n<</Size %d/Root 1 0 R>>\nstartxref\n%d\n%%%%EOF\n"
            % (len(objs), xref))
    path.write_bytes(bytes(out))


def png_size(path):
    b = path.read_bytes()
    return struct.unpack(">II", b[16:24])


def load_words(boxes):
    """(name, family, width, height, path), grouped by family."""
    by_height = {v[0]: fam for fam, v in boxes.items()}
    out = {}
    for f in sorted(SRC.glob("*.png")):
        w, h = png_size(f)
        if h not in by_height:
            sys.exit("%s is %dpx tall, which matches no family in "
                     "geometry.h (%s).\nThe word images are older than "
                     "geometry.h - re-run tools/tune.py first."
                     % (f.name, h, ", ".join("%s %d" % (k, v[0])
                                             for k, v in sorted(boxes.items()))))
        fam = by_height[h]
        out.setdefault(fam, []).append((f.stem, fam, w, h, f))
    return out


def already_drawn():
    """Families that have ink extracted from them already.

    Regenerating a template moves every box on the page, so any drawing done
    against the old one would be cropped from the wrong places. That is worth
    refusing rather than warning about: the damage is silent, and it looks
    like bad handwriting rather than like a tooling mistake.
    """
    if not INKINDEX.exists():
        return set()
    try:
        return {r["family"] for r in json.loads(INKINDEX.read_text())}
    except (ValueError, KeyError):
        return set()


def budget_for(fam, widest):
    if fam in FULL_WIDTH:
        return MAX_WORD_W
    return int(widest * HEADROOM + 0.5)


def scale_for(fam, widest):
    """One pen width for the whole job - see the module docstring."""
    s = max(2, round(PEN_PX / (INK_RATIO * X_HEIGHT[fam])))
    # ...but never so wide that the box runs off the page, which binds for
    # TIME and SOLO and for nothing else.
    return min(s, (PAGE_W - 2 * MARGIN) // budget_for(fam, widest))


def font(size):
    for p in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
              "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"):
        if Path(p).exists():
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def reference(path, scale):
    """The template word, inverted to black-on-white and scaled up.

    Inverted because the watchface's default is white ink on black paper,
    and a black slab beside the space you are writing in is a poor thing to
    judge letterforms against. Smoothed on the way up: this is a guide to
    the shape and the width, not something to trace, and the pixel edges of
    a 51px original are not information.
    """
    im = Image.open(path).convert("L")
    im = Image.eval(im, lambda v: 255 - v)
    return im.resize((im.width * scale, im.height * scale), Image.LANCZOS)


def draw_rules(d, top, base, xheight, box_h, scale, x0, x1, faint=False):
    """Four rules: ascender limit, mean line, baseline, descender limit.

    The first drawn page overran the box on every single word - ascenders by
    up to 24px and descenders by 20 - because the only thing marking those
    limits was the faint outline of the box, and the eye does not read a
    rectangle as a line to stop at. They are proper rules now.

    They are also not decoration. The box is exactly the height the
    watchface gives this family, so ink outside it is ink that gets clipped.

    Weighted deliberately: the BASELINE is the heaviest, because sitting the
    letters on it consistently is what stops the finished line looking
    drunk. The two limits are next. The mean line is the faintest - x-height
    wants to be even, but it is a matter of degree, where the other three
    are pass or fail.

    Full width for the box being written in, so there is something to keep a
    hand level against right across the page. Only as wide as the word for
    the reference above it - eight rules crossing every page was unreadable.
    """
    y_base = top + base * scale
    y_mean = y_base - xheight * scale
    y_asc = top
    y_desc = top + box_h * scale
    if faint:
        for y in (y_base, y_mean):
            d.line([(x0, y), (x1, y)], fill=GUIDE_FAINT, width=2)
        return y_base, y_mean
    d.line([(x0, y_asc), (x1, y_asc)], fill=GUIDE, width=2)
    d.line([(x0, y_desc), (x1, y_desc)], fill=GUIDE, width=2)
    d.line([(x0, y_mean), (x1, y_mean)], fill=GUIDE_MEAN, width=2)
    d.line([(x0, y_base), (x1, y_base)], fill=GUIDE_BASE, width=3)
    return y_base, y_mean


def build(fam, words, scale, budget, boxes):
    """One PDF per family. Returns (pages, manifest)."""
    box_h, base = boxes[fam]
    xheight = X_HEIGHT[fam]

    f_label = font(30)
    f_small = font(22)
    f_head = font(24)

    draw_w = budget * scale
    ref_h = box_h * scale
    block_h = 34 + ref_h + 26 + ref_h + 40
    top0 = 116
    avail = PAGE_H - top0 - 40
    per_page = max(1, avail // block_h)
    # Spread whatever is left between the blocks rather than pooling it at
    # the bottom, which reads as an unfinished page.
    slack = (avail - per_page * block_h) // max(1, per_page - 1) if per_page > 1 else 0

    pages, manifest = [], []
    for i in range(0, len(words), per_page):
        chunk = words[i:i + per_page]
        page = Image.new("L", (PAGE_W, PAGE_H), 255)
        d = ImageDraw.Draw(page)
        n = i // per_page + 1
        total = (len(words) + per_page - 1) // per_page
        draw_mark(d, fam, n - 1)
        d.text((MARGIN, 36), "%s family  -  page %d of %d  -  scale %dx"
               % (fam, n, total, scale), font=f_head, fill=LABEL)
        d.text((MARGIN, 64),
               "keep ink INSIDE the box: anything past the top or bottom "
               "rule is clipped on the watch", font=f_small, fill=GUIDE)
        d.line([(MARGIN, 96), (PAGE_W - MARGIN, 96)], fill=GUIDE_FAINT, width=2)

        y = top0
        for (name, _, w, h, path) in chunk:
            d.text((MARGIN, y), name, font=f_label, fill=LABEL)
            y += 34

            page.paste(reference(path, scale), (MARGIN, y))
            draw_rules(d, y, base, xheight, box_h, scale,
                       MARGIN, MARGIN + w * scale + 20, faint=True)
            y += ref_h + 26

            # The box to draw in: always the full width budget, so a word
            # can be made wider than the font's if that reads better. The
            # tick shows where the font ended.
            draw_rules(d, y, base, xheight, box_h, scale, 0, PAGE_W)
            d.rectangle([MARGIN, y, MARGIN + draw_w, y + ref_h],
                        outline=GUIDE_FAINT, width=2)
            tick = MARGIN + w * scale
            d.line([(tick, y), (tick, y + 16)], fill=GUIDE, width=3)
            d.line([(tick, y + ref_h - 16), (tick, y + ref_h)], fill=GUIDE, width=3)
            d.text((tick + 8, y + ref_h + 6), "font width", font=f_small, fill=GUIDE)
            gx = MARGIN + draw_w + 14
            d.text((gx, y + 4), "ascender limit", font=f_small, fill=GUIDE)
            d.text((gx, y + base * scale - xheight * scale + 4), "mean",
                   font=f_small, fill=GUIDE_MEAN)
            d.text((gx, y + base * scale + 4), "BASELINE",
                   font=f_small, fill=GUIDE_BASE)
            d.text((gx, y + ref_h - 26), "descender limit",
                   font=f_small, fill=GUIDE)

            manifest.append({
                "word": name, "family": fam, "page": n - 1,
                "box": [MARGIN, y, draw_w, ref_h],
                "scale": scale,
                "baseline": base * scale,
                "xheight": xheight * scale,
                "font_width": w * scale,
                "target": [w, h],
            })
            y += ref_h + 40 + (slack if name != chunk[-1][0] else 0)

        pages.append(page.convert("RGB"))
    return pages, manifest


def main():
    args = [a for a in sys.argv[1:] if a != "--force"]
    force = "--force" in sys.argv[1:]
    want = args or None

    OUT.mkdir(parents=True, exist_ok=True)
    boxes = family_boxes()
    grouped = load_words(boxes)
    drawn = already_drawn()
    everything = []

    # Regenerating a family that has already been drawn would move every box
    # on its pages, so the existing ink would be cropped from the wrong
    # places. Refuse by default; --force is there for when the redraw is the
    # intention. Note this rewrites the manifest for EVERY family it runs
    # for, so a partial run still has to leave the untouched families' boxes
    # exactly where they were - which it does, because their inputs and
    # arithmetic have not changed.
    blocked = sorted(f for f in (want or grouped) if f in drawn)
    if blocked and not force:
        sys.exit(
            "refusing to regenerate %s: those families already have drawn "
            "ink.\nRegenerating moves the boxes on the page, so pages drawn "
            "against the old\ntemplate would be cropped from the wrong "
            "places.\n\nIf you mean to redraw them, pass --force. If you "
            "only wanted to pick up a\nchanged canvas size, you do not need "
            "to: tools/extract_handwriting.py anchors\non the baseline and "
            "copes with a template whose box no longer matches."
            % ", ".join(blocked))

    for fam, words in grouped.items():
        if want and fam not in want:
            continue
        widest = max(w for (_, _, w, _, _) in words)
        scale = scale_for(fam, widest)
        budget = budget_for(fam, widest)
        pages, manifest = build(fam, words, scale, budget, boxes)
        pdf = OUT / ("handwritten_%s.pdf" % fam)
        write_pdf(pdf, pages, PAGE_PT)
        everything += manifest
        print("%-6s %2d words  widest %3dpx  box %3dpx x %2dpx  scale %2dx  "
              "x-height %3dpx  %2d pages"
              % (fam, len(words), widest, budget, boxes[fam][0], scale,
                 X_HEIGHT[fam] * scale, len(pages)))

    # A partial run must not drop the other families out of the manifest.
    if want and (OUT / "manifest.json").exists():
        keep = [e for e in json.loads((OUT / "manifest.json").read_text())
                if e["family"] not in want]
        everything = keep + everything

    (OUT / "manifest.json").write_text(json.dumps(everything, indent=1))
    print("manifest: %d words -> %s" % (len(everything), OUT / "manifest.json"))


if __name__ == "__main__":
    main()
