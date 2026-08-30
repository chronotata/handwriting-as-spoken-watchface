#!/usr/bin/env python3
"""
Does geometry.h describe the images that are actually on disk?

    python3 tools/check_resources.py

Exits non-zero and names every mismatch. Run by tools/test/run.sh, and by
tools/date_sweep.sh before it screenshots anything.


WHY THIS EXISTS SEPARATELY FROM tune.py
---------------------------------------
tune.py has the same check, but it runs it just BEFORE writing geometry.h -
it catches an image that is the wrong size at generation time. That leaves a
gap it cannot see: the images changing AFTERWARDS, with geometry.h left
holding numbers that were true when it was written and are not any more.

Which is not hypothetical. extract_handwriting.py was fixed to take a word's
width from the band rather than from the padded region, several words came
out one or two pixels narrower, and tune.py was not re-run. geometry.h went
on declaring the old widths. On the watch, a bitmap drawn into a rect wider
than itself repeats - so five of the ten digits grew a sliver of their own
left edge on the right, and the result read as "the sweep is showing an old
version of the handwriting".

Nothing stopped that build. run.sh warned that the ink was newer than
geometry.h, but a warning during a test run is not in the path between
editing and looking at the result. This is, and it is a hard failure.
"""

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GEOM = ROOT / "src" / "c" / "geometry.h"
IMGDIR = ROOT / "resources" / "images"


def png_size(path):
    b = path.read_bytes()
    if b[:8] != b"\x89PNG\r\n\x1a\n" or b[12:16] != b"IHDR":
        sys.exit("%s is not a PNG" % path)
    return struct.unpack(">II", b[16:24])


def main():
    if not GEOM.exists():
        sys.exit("src/c/geometry.h is missing - run tools/tune.py")
    text = GEOM.read_text()

    # Both typefaces. The font's rows name W_*, the handwriting's H_*, and
    # an undrawn word repeats its font row - so the same resource can appear
    # twice and be checked twice, which costs nothing.
    rows = re.findall(r"\{RESOURCE_ID_(\w+),\s*(\d+),\s*(\d+),\s*(\d+)\}", text)
    if not rows:
        sys.exit("no WORDS[] rows in geometry.h - run tools/tune.py")

    problems, checked = [], set()
    for name, w, h, _base in rows:
        stem = ("h_" + name[2:] if name.startswith("H_") else name).lower()
        key = (stem, int(w), int(h))
        if key in checked:
            continue
        checked.add(key)
        path = IMGDIR / ("%s.png" % stem)
        if not path.exists():
            problems.append("  %-16s declared %sx%s, but %s is missing"
                            % (stem, w, h, path.name))
            continue
        aw, ah = png_size(path)
        if (aw, ah) != (int(w), int(h)):
            problems.append("  %-16s declared %sx%s, image is %dx%d"
                            % (stem, w, h, aw, ah))

    if problems:
        print("geometry.h does not match the images on disk:\n"
              + "\n".join(sorted(problems)), file=sys.stderr)
        print("\nA bitmap drawn into a rect that is not its own size repeats "
              "to fill it, so\nthis shows up as smeared or doubled letters "
              "rather than as an error.\n\n"
              "    python3 tools/extract_handwriting.py   # if the ink changed"
              "\n    python3 tools/tune.py                  # always",
              file=sys.stderr)
        if any(p.lstrip().startswith("w_") for p in problems):
            print("\nThe FONT images are among the mismatches. tune.py "
                  "renders those itself, so\nit cannot be looking at an old "
                  "copy of them - it means geometry.h was NOT\nwritten by "
                  "the run that wrote these images. Two ways that happens:\n\n"
                  "  - the last tune.py run aborted. It writes the images in "
                  "pass 2 and\n    geometry.h afterwards, so a run that stops "
                  "in between leaves exactly\n    this. Read what it said and "
                  "run it again.\n\n"
                  "  - geometry.h came off a different machine. The family "
                  "boxes are measured\n    from rendered ink, and FreeType "
                  "differs by a pixel between builds, so\n    generated files "
                  "only describe the machine that made them. Re-run\n    "
                  "tune.py here and commit what it writes.",
                  file=sys.stderr)
        return 1

    print("  resources            %d word images match geometry.h"
          % len(checked))
    return 0


if __name__ == "__main__":
    sys.exit(main())
