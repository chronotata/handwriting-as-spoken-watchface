#!/usr/bin/env python3
"""
Turn annotated Supernote pages into watchface bitmaps.

    python3 tools/extract_handwriting.py

Reads every PDF in handwriting-templates/drawn/, finds the words drawn on
them, and writes:

    resources/images/h_<word>.png        palettised, ready for the SDK
    handwriting-templates/ink/<word>.png greyscale, for looking at
    handwriting-templates/ink/index.json widths and ink extents, for tune.py

Run this whenever a new page is drawn. Then run tools/tune.py, which is what
actually writes geometry.h and package.json.


WHY THIS IS A SEPARATE TOOL FROM tune.py
----------------------------------------
tune.py rasterises a TTF through FreeType, and FreeType's hinting differs by
a pixel between machines - which is why the generated files have to come
from one machine and nowhere else. Nothing here touches a font. This is
arithmetic on a PNG, so it produces the same bytes anywhere and can be run
by anyone, including in a sandbox.


HOW A WORD IS FOUND ON THE PAGE
-------------------------------
The Nomad stores annotations as a separate transparent image over the page,
and that image is 1404x1872 - the screen buffer - regardless of the page's
physical size. The templates are laid out in exactly those pixels, so the
box a word was drawn in IS the box to crop, with no registration step and no
rescaling. handwriting-templates/out/manifest.json holds the boxes.

Vertically the crop is the box exactly, never the ink: the box is the
family's canvas, so cropping to it is what puts the baseline where the
watchface expects it. Ink outside the box is ink the watchface would clip,
so it is reported and then dropped.

Horizontally the crop IS the ink, because a word's width is its own - that
is the whole reason WORDS[] carries a width per word.
"""

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required:  "
             "pip install Pillow --break-system-packages")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tune                                    # noqa: E402  (for to_palette)

ROOT = Path(__file__).resolve().parent.parent
DRAWN = ROOT / "handwriting-templates" / "drawn"
INK = ROOT / "handwriting-templates" / "ink"
IMGDIR = ROOT / "resources" / "images"
MANIFEST = ROOT / "handwriting-templates" / "out" / "manifest.json"
GEOM = ROOT / "src" / "c" / "geometry.h"

# The same stretch tune.py applies to the font, and for the same reason: the
# supersampled edge needs pulling back to something with a definite edge, or
# every word arrives as a grey smudge. Sharing the numbers is what keeps the
# two typefaces looking like they belong on the same screen.
INK_LO, INK_HI = tune.INK_LO, tune.INK_HI

# Alpha below this is the anti-aliased skirt of a stroke rather than the
# stroke, and including it in the bounding box widens every word by a pixel
# or two of nothing.
EDGE = 40


def family_boxes():
    """The canvas each family uses, read back out of geometry.h.

    Deliberately NOT recomputed from the handwriting. The boxes are the
    font's, the templates were drawn against the font's, and holding them
    fixed is what lets the two typefaces be swapped on the watch without a
    single word moving - which is the entire point of keeping both.
    """
    if not GEOM.exists():
        sys.exit("src/c/geometry.h is missing - run tools/tune.py first")
    text = GEOM.read_text()
    out = {}
    import re
    for fam in ("TIME", "SOLO", "MINS", "DATE", "ORD", "HEDGE"):
        h = re.search(r"^#define %s_BOX_H\s+(\d+)" % fam, text, re.M)
        b = re.search(r"^#define %s_BOX_BASE\s+(\d+)" % fam, text, re.M)
        if not h or not b:
            sys.exit("geometry.h has no %s box - run tools/tune.py" % fam)
        out[fam] = (int(h.group(1)), int(b.group(1)))
    return out


def annotation_layers(pdf):
    """-> {page number (1-based): (page background, ink alpha)}

    The ink is the SMASK of the overlay image, which pdfimages writes as a
    separate greyscale file; a page with no annotation has no mask at all.

    The background is wanted too, because that is where the page says which
    family and page it is. Of the two RGB images on an annotated page, the
    ink's colour layer is a flat slab - it carries no picture, only the pen
    colour - so the background is the one with any variation in it. Picking
    by variance rather than by position means this does not depend on the
    order the exporter happens to write objects in.
    """
    out = {}
    listing = subprocess.run(["pdfimages", "-list", str(pdf)],
                             capture_output=True, text=True, check=True).stdout
    pages = sorted({int(line.split()[0])
                    for line in listing.splitlines()[2:]
                    if len(line.split()) > 2 and line.split()[2] == "smask"})
    if not pages:
        return out
    with tempfile.TemporaryDirectory() as td:
        for p in pages:
            subprocess.run(["pdfimages", "-png", "-f", str(p), "-l", str(p),
                            str(pdf), "%s/p%d" % (td, p)], check=True)
            files = sorted(Path(td).glob("p%d-*.png" % p))
            masks = [f for f in files if Image.open(f).mode == "L"]
            colour = [f for f in files if Image.open(f).mode != "L"]
            if not masks or not colour:
                continue
            best, spread = None, -1
            for f in colour:
                im = Image.open(f).convert("L")
                lo, hi = im.getextrema()
                if hi - lo > spread:
                    best, spread = im, hi - lo
            out[p] = (best, Image.open(masks[-1]).convert("L"))
    return out


# Must match draw_mark() in tools/make_templates.py.
FAMILY_ORDER = ["TIME", "SOLO", "HEDGE", "MINS", "DATE", "ORD"]
MARK_CELL, MARK_Y, MARK_BITS, MARGIN = 20, 10, 12, 60


def read_mark(background):
    """-> (family, page index) or None if the page does not carry a mark.

    Sampled well inside each cell so that JPEG ringing at the edges, and any
    half-pixel the exporter introduces, cannot reach the sample point.
    """
    if background is None or background.width < 400:
        return None
    x0 = background.width - MARGIN - MARK_BITS * MARK_CELL
    y = MARK_Y + MARK_CELL // 2 - 2
    bits = []
    for i in range(MARK_BITS):
        cx = x0 + i * MARK_CELL + (MARK_CELL - 4) // 2
        if not (0 <= cx < background.width and 0 <= y < background.height):
            return None
        bits.append(background.getpixel((cx, y)) < 128)
    if not bits[0] or sum(bits[:-1]) % 2 != (1 if bits[-1] else 0):
        return None                              # no start bit, or parity fails
    fam = (bits[1] << 2) | (bits[2] << 1) | bits[3]
    if fam >= len(FAMILY_ORDER):
        return None
    page = 0
    for b in bits[4:11]:
        page = (page << 1) | int(b)
    return FAMILY_ORDER[fam], page


def family_from_name(pdf):
    """Fallback for pages drawn before the mark existed."""
    upper = pdf.stem.upper()
    hits = [f for f in FAMILY_ORDER if f in upper]
    return hits[0] if len(hits) == 1 else None


def extract(alpha, entry, box_h, box_base):
    """One word: crop, reduce, stretch. -> (image, report) or (None, report)."""
    bx, by, bw, bh = entry["box"]
    scale = entry["scale"]
    name = entry["word"]

    # A generous margin, only so overrun can be MEASURED before it is dropped.
    pad = box_h * scale
    y0, y1 = max(0, by - pad), min(alpha.height, by + bh + pad)
    region = alpha.crop((bx, y0, bx + bw, y1))
    if sum(region.histogram()[129:]) < 200:
        return None, None                       # nothing drawn here

    ink = region.point(lambda v: 255 if v > EDGE else 0)
    left, top, right, bot = ink.getbbox()       # right/bot are exclusive
    over_top = max(0, by - (top + y0))
    over_bot = max(0, (bot + y0) - (by + bh))

    # Vertically the box, horizontally the ink - see the module docstring.
    band = region.crop((left, by - y0, right, by - y0 + bh))
    small = band.resize((max(1, round(band.width / scale)), box_h),
                        Image.LANCZOS)

    # The same contrast stretch tune.py applies, expressed the same way it
    # does - a 256-entry lookup - so the two cannot drift apart.
    img = small.point([0 if v < INK_LO else
                       (255 if v > INK_HI else
                        int(255 * (v - INK_LO) / (INK_HI - INK_LO)))
                       for v in range(256)])

    seen = img.point(lambda v: 255 if v > 8 else 0).getbbox()
    asc = box_base - seen[1] if seen else 0
    desc = seen[3] - box_base if seen else 0

    return img, {
        "word": name, "family": entry["family"],
        "w": img.width, "h": img.height, "base": box_base,
        "asc": asc, "desc": desc,
        "over_top": round(over_top / scale, 1),
        "over_bot": round(over_bot / scale, 1),
        "font_w": entry["target"][0],
    }


def main():
    if not shutil.which("pdfimages"):
        sys.exit("pdfimages is required to read the annotation layer out of "
                 "the PDF:\n    sudo apt install poppler-utils")
    if not MANIFEST.exists():
        sys.exit("no manifest - run tools/make_templates.py first")
    entries = json.load(MANIFEST.open())
    by_cell = {}
    for e in entries:
        by_cell.setdefault((e["family"], e["page"]), []).append(e)
    boxes = family_boxes()

    # Oldest first, so that when a word appears in more than one export the
    # most recent drawing of it wins. Alphabetical order would have made
    # "..._test2.pdf" beat "..._final.pdf", which is precisely backwards.
    pdfs = sorted(DRAWN.glob("*.pdf"), key=lambda f: f.stat().st_mtime)
    if not pdfs:
        sys.exit("no PDFs in %s - export the drawn pages there" % DRAWN)

    INK.mkdir(parents=True, exist_ok=True)
    IMGDIR.mkdir(parents=True, exist_ok=True)

    found, reports, unknown = {}, [], 0
    for pdf in pdfs:
        layers = annotation_layers(pdf)
        if not layers:
            print("  %-32s no annotations" % pdf.name)
            continue
        guessed = None
        hits = 0
        for page_no in sorted(layers):
            background, alpha = layers[page_no]
            mark = read_mark(background)
            if mark is None:
                # Drawn on a template from before the page carried its own
                # identity. Fall back to the filename, and say so - a wrong
                # guess here silently crops the wrong boxes out of the page,
                # which is exactly the bug the mark was added to stop.
                if guessed is None:
                    guessed = family_from_name(pdf)
                    if guessed:
                        print("  %-32s no page marks; taking family %s from "
                              "the filename" % (pdf.name, guessed))
                if guessed is None:
                    unknown += 1
                    continue
                mark = (guessed, page_no - 1)
            fam, page = mark
            box_h, box_base = boxes[fam]
            for e in by_cell.get((fam, page), []):
                img, rep = extract(alpha, e, box_h, box_base)
                if img is None:
                    continue
                if e["word"] in found:
                    print("  %-32s %s was already drawn in an older export - "
                          "using this one" % (pdf.name, e["word"]))
                rep["source"] = "%s p%d" % (pdf.name, page_no)
                found[e["word"]] = img
                reports.append(rep)
                hits += 1
        print("  %-32s %d words" % (pdf.name, hits))

    if unknown:
        print("\n%d page(s) carried no mark and the family could not be taken "
              "from the filename.\nRename the export to contain the family, "
              "or redraw on a current template." % unknown)
    if not found:
        sys.exit("no drawn words found")

    # The palette encoding is tune.py's, imported rather than reimplemented:
    # the level->colour mapping is decoded by tint() in the C, and two copies
    # of it would be two things to keep in step.
    total = 0
    for word, img in sorted(found.items()):
        img.save(INK / ("%s.png" % word))
        stem = "h_" + (word[2:] if word.startswith("w_") else word)
        path = IMGDIR / ("%s.png" % stem)
        # No bolder ring: that exists to put weight back into a font that
        # renders thin, and a pressure pen has already put it there.
        tune.to_palette(img, img).save(path, transparency=0)
        total += path.stat().st_size

    # Anything previously extracted but not drawn this time is stale - the
    # same reasoning as tune.py pruning orphaned word images.
    keep = {"h_" + (w[2:] if w.startswith("w_") else w) for w in found}
    stuck = []
    for stale in sorted(IMGDIR.glob("h_*.png")):
        if stale.stem in keep:
            continue
        try:
            stale.unlink()
            print("  removed %s (no longer drawn)" % stale.name)
        except OSError:
            # Some mounts allow writing but not unlinking. Losing the whole
            # extraction over a leftover file would be the wrong trade, but
            # leaving it unmentioned would ship a word nobody drew.
            stuck.append(stale.name)
    if stuck:
        print("\ncould not delete %d stale image(s); package.json will not "
              "reference them,\nbut they should go: %s"
              % (len(stuck), " ".join(stuck)))

    reports.sort(key=lambda r: r["word"])
    (INK / "index.json").write_text(json.dumps(reports, indent=1) + "\n")

    over = [r for r in reports if r["over_top"] > 3 or r["over_bot"] > 3]
    print("\n%-14s %6s %6s %8s   %s"
          % ("word", "width", "font", "vs font", "clipped"))
    for r in reports:
        note = ""
        if r["over_top"] or r["over_bot"]:
            note = "%+.1f top  %+.1f bottom" % (r["over_top"], r["over_bot"])
        print("%-14s %6d %6d %7d%%   %s"
              % (r["word"], r["w"], r["font_w"],
                 round(100 * r["w"] / r["font_w"]), note))
    print("\n%d words, %.1f kB -> resources/images/h_*.png"
          % (len(found), total / 1024))
    print("index -> handwriting-templates/ink/index.json")
    if over:
        print("\n%d word(s) lost more than 3px to the box edge. Under that is "
              "rounding;\nmore than that, redraw inside the rules." % len(over))
    print("\nnow run: python3 tools/tune.py")


if __name__ == "__main__":
    main()
