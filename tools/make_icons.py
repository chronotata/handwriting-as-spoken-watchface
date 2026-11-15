#!/usr/bin/env python3
"""Make the watchface's icons from the hand-drawn pages.

    python3 tools/make_icons.py
    python3 tools/make_icons.py --menu-word to --store-word past

Writes:

    resources/images/menu_icon.png   25x25   the WATCH launcher icon
    store/icon-144.png               144x144 the appstore's large icon
    store/icon-80.png                80x80   the appstore's small icon

Then run tools/tune.py, which lists menu_icon.png in package.json.


TWO DIFFERENT ICONS, TWO DIFFERENT PLACES
-----------------------------------------
The watch's own list of faces uses a 25x25 PNG built INTO the .pbw - a
bitmap resource marked "menuIcon": true. Without one, a sideloaded build
shows the default icon. The appstore's icons are uploaded separately in the
Developer Dashboard and are used by the phone app and the store; they never
reach the watch. Pebble's docs are explicit that the two are independent.

So they are chosen separately, and they want different words. At 144px any
word reads; at 25px only a very short one survives - "past" turns into a
scribble, while "to" keeps a crossed t and a round o and is unmistakably
pen-drawn.


WHY THEY ARE CUT FROM THE PDF, NOT FROM THE WATCH BITMAPS
---------------------------------------------------------
The watch bitmaps are the words at watch size - "past" is 69x51 - and
enlarging one to 144px gives blocky pixels. The drawn pages hold the same
strokes at 4-15x that resolution, so every icon here is scaled DOWN from the
real pen line, the same way extract_handwriting.py makes the watch images.
Nothing about this touches a font, so it gives the same bytes anywhere.


WHITE ON A BLACK TILE, EVEN ON THE WATCH
----------------------------------------
On colour platforms a watchface's menu icon is drawn in colour with its
transparency respected. White ink on a transparent ground would vanish on a
light launcher row, so the icon carries its own black tile - which is also
what the face looks like by default. It is quantised to the four greys Emery
can show, so what is checked in is what the watch draws.
"""

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import extract_handwriting as ex                # noqa: E402  (PDF ink layers)

from PIL import Image                           # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
INDEX = ROOT / "handwriting-templates" / "ink" / "index.json"
MANIFEST = ROOT / "handwriting-templates" / "out" / "manifest.json"
MENU_ICON = ROOT / "resources" / "images" / "menu_icon.png"
STORE = ROOT / "store"

GREYS = (0, 85, 170, 255)           # every grey a 2-bit-per-channel panel has


def pen_strokes(word, cache):
    """The word at full drawn resolution, trimmed to its ink."""
    idx = {r["word"]: r for r in json.loads(INDEX.read_text())}
    man = json.loads(MANIFEST.read_text())
    man = {w["word"]: w for w in (man if isinstance(man, list) else man["words"])
           if isinstance(w, dict) and "word" in w}
    key = "w_" + word
    if key not in idx or key not in man:
        sys.exit("no drawn word %r - run tools/extract_handwriting.py, or "
                 "check the spelling (eg 'to', 'past', 'half')" % word)
    m = re.match(r"(.*\.pdf) p(\d+)$", idx[key]["source"])
    fname, page = m.group(1), int(m.group(2))
    if fname not in cache:
        cache[fname] = ex.annotation_layers(ex.DRAWN / fname)
    alpha = cache[fname][page][1].convert("L")
    x, y, w, h = man[key]["box"]
    crop = alpha.crop((x - 20, y - h // 2, x + w + 20, y + h + h // 2))
    bbox = crop.point(lambda v: 255 if v > 40 else 0).getbbox()
    if not bbox:
        sys.exit("found no ink for %r on %s page %d" % (word, fname, page))
    return crop.crop(bbox)


def tile(ink, size, margin, quantise=False):
    box = size - 2 * margin
    k = min(box / ink.width, box / ink.height)
    w, h = max(1, round(ink.width * k)), max(1, round(ink.height * k))
    out = Image.new("L", (size, size), 0)
    out.paste(ink.resize((w, h), Image.LANCZOS), ((size - w) // 2, (size - h) // 2))
    if quantise:
        out = out.point(lambda v: min(GREYS, key=lambda g: abs(g - v)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--menu-word", default="to",
                    help="word for the 25x25 watch icon (default: to)")
    ap.add_argument("--store-word", default="past",
                    help="word for the appstore icons (default: past)")
    args = ap.parse_args()

    cache = {}
    menu = tile(pen_strokes(args.menu_word, cache), 25, 2, quantise=True)
    # Palettised, four greys: the SDK takes it as-is, and the file shows
    # exactly the levels the panel will.
    pal = menu.convert("P", palette=Image.ADAPTIVE, colors=len(GREYS))
    pal.save(MENU_ICON, optimize=True)
    print("  %-34s 25x25    \"%s\"   (watch launcher)" % (
        MENU_ICON.relative_to(ROOT), args.menu_word))

    STORE.mkdir(exist_ok=True)
    ink = pen_strokes(args.store_word, cache)
    for size in (144, 80):
        p = STORE / ("icon-%d.png" % size)
        tile(ink, size, round(size * 0.14)).convert("RGB").save(p, optimize=True)
        print("  %-34s %dx%-4d \"%s\"   (appstore)" % (
            p.relative_to(ROOT), size, size, args.store_word))

    print("\nnow run: python3 tools/tune.py   (adds the menu icon to package.json)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
