#!/usr/bin/env python3
"""Did any screenshot catch the emulator showing the real time?

    python3 tools/check_shots.py shots/stack-*.png

Takes shots that are all supposed to carry THE SAME DATE, and names any
whose date line differs from the rest. Run by tools/date_sweep.sh over the
fixed-date sweeps (times, stacks, hours); the days and dates sweeps change
the date on purpose, so it does not apply to them.


WHY THIS CAN BE DETECTED AT ALL
-------------------------------
Nothing about a wrong shot is wrong-looking. It is a real screenshot of a
real face, correctly drawn, of the wrong moment - so it survives every check
that asks whether the picture is sound.

But the sweep pins the date. Every shot in a fixed-date sweep shows 22
August 2026, and a shot that slipped back to the emulator's real clock shows
today's date instead. The date line is at a fixed baseline, below
DATE_TOP_LIMIT, which the time rows are asserted never to reach - so the
bottom band of the image is the date and nothing else, and comparing that
band across the set finds the odd ones out without reading a single letter.

Majority vote rather than a fixed expectation: the tool does not know what
date was asked for, only that they should all agree.


WHY IT IS NEEDED ON TOP OF THE DUPLICATE CHECK
----------------------------------------------
date_sweep.sh already compares each shot to the one before it, which catches
a face that did not repaint. It cannot catch this: a real-time face is not a
duplicate of anything, because real time keeps advancing. Two shots out of
59 came back showing the real date this way, and the only thing that noticed
was a person looking at them.
"""

import re
import sys
from collections import Counter
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required:  pip install Pillow --break-system-packages")

ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "src" / "c" / "config.h"


def date_top():
    """The first row the date can occupy, read from config.h rather than
    guessed - if the layout moves, this moves with it."""
    m = re.search(r"^#define DATE_TOP_LIMIT\s+(\d+)", CONFIG.read_text(), re.M)
    if not m:
        sys.exit("no DATE_TOP_LIMIT in src/c/config.h")
    return int(m.group(1))


def band(path, top):
    im = Image.open(path).convert("L")
    return im.crop((0, top, im.width, im.height)).tobytes()


def main(argv):
    files = [Path(a) for a in argv]
    if len(files) < 3:
        return 0            # a majority of two proves nothing

    top = date_top()
    seen = {}
    for f in files:
        if f.exists():
            seen[f] = hash(band(f, top))

    if not seen:
        return 0
    counts = Counter(seen.values())
    if len(counts) == 1:
        print("  shots                %d date lines all agree" % len(seen))
        return 0

    good, _n = counts.most_common(1)[0]
    odd = sorted(f for f, h in seen.items() if h != good)
    print("%d of %d shots have a DIFFERENT DATE LINE from the rest:"
          % (len(odd), len(seen)), file=sys.stderr)
    for f in odd:
        print("  %s" % f.name, file=sys.stderr)
    print("\nThe sweep pins the date, so these are almost certainly the "
          "emulator's\nreal clock rather than the time asked for - see "
          "WHY IT SETS THE CLOCK EARLY\nin tools/date_sweep.sh. Re-run just "
          "those times by name; if it keeps\nhappening, lower AFTER so less "
          "time passes between the rollover and the\nscreenshot.",
          file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
