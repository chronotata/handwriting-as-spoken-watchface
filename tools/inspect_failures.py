#!/usr/bin/env python3
"""Render every interweave failure the test suite reports, one picture each.

    python3 tools/inspect_failures.py

Writes shots-failure-inspect/, plus an index.txt listing what each picture
is and every time it stands for.


WHY ONE PICTURE PER COLLISION, NOT PER FAILURE
----------------------------------------------
The suite counts a failure for every minute and every combination of
settings it occurs under, which is right for a test and useless for looking
at. The same two words touching in the same way fail once per hour, once per
date format and once per "minutes" setting - 288 failures turned out to be
two collisions. So this groups by what actually collided (the reading mode,
the two words, and by how much) and renders one representative of each,
with the full list of times in the index.

The picture is the shipping layout, not a model: the harness says which
cases failed, tools/test/dump_layout.c - the real refresh() and draw path -
says where every word goes, and tools/mock_sweep.py's renderer pastes the
real bitmaps there. Same pipeline as the mock sweep.

Nothing is written to the tree except the output folder. The harness is
built and run in a temporary directory, so its reachability.json stays put.
"""

import collections
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mock_sweep                               # noqa: E402  (the renderer)

from PIL import Image                           # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
TESTDIR = ROOT / "tools" / "test"
OUT = ROOT / "shots-failure-inspect"

ROUNDING = {0: "exact", 1: "five", 2: "spoken"}
MINUTES = {0: "auto", 1: "never", 2: "always"}

# The harness sweeps every minute of 21 August 2026 (make_tm(232, 21, 7,
# 2026, ...)), so the pictures use the same date and the date line matches.
DATE = "2026-08-21"
SCALE = 3


def word_text():
    src = (TESTDIR / "generated.h").read_text()
    block = src.split("static const char *const WORD_TEXT[] = {")[1]
    return re.findall(r'^\s*"(.*)",\s*$', block.split("};")[0], re.M)


def build(tmp):
    common = ["gcc", "-std=c11", "-I.", "-Wall", "-Wno-unused-function",
              "-Wno-unused-parameter", "-O1"]
    for exe, src in (("harness", "harness.c"), ("dump_layout", "dump_layout.c")):
        cc = subprocess.run(common + ["-o", str(tmp / exe), src, "stub.c"],
                            cwd=TESTDIR, capture_output=True, text=True)
        if cc.returncode:
            sys.exit("could not build tools/test/%s:\n%s" % (src, cc.stderr))


def main():
    texts = word_text()
    tmp = Path(tempfile.mkdtemp())
    try:
        build(tmp)
        log = tmp / "interweave.txt"
        # Run in tmp: the harness writes reachability.json to its cwd.
        subprocess.run([str(tmp / "harness")], cwd=tmp, capture_output=True,
                       env={"HARNESS_INTERWEAVE_LOG": str(log), "PATH": "/usr/bin:/bin"})
        if not log.exists():
            sys.exit("the harness wrote no failure log - is it the current "
                     "tools/test/harness.c?")
        rows = [tuple(map(int, l.split())) for l in log.read_text().splitlines()
                if l.strip()]

        if OUT.exists():
            shutil.rmtree(OUT)          # this folder is ours alone; start clean
        OUT.mkdir()

        if not rows:
            (OUT / "index.txt").write_text("No interweave failures.\n")
            print("no interweave failures - nothing to inspect")
            return 0

        # h m rounding minutes date_format upper lower overlap limit asc
        groups = collections.OrderedDict()
        for h, m, rnd, mins, fmt, up, lo, ov, lim, asc in rows:
            key = (rnd, mins, up, lo, ov, lim)
            groups.setdefault(key, set()).add((h, m))

        res = mock_sweep.resource_map()
        ring_top = mock_sweep.cfg("BOLD_RING_TOP", 2)
        cache = {}
        index = ["%d interweave failures, %d distinct collisions.\n" % (
                 len(rows), len(groups)),
                 "Each picture is one representative; every time it stands for "
                 "is listed under it.\n"
                 "Rendered at %dx. Limit = INK_OVERLAP_MAX_PCT of the lower "
                 "word's ascender.\n" % SCALE]

        for (rnd, mins, up, lo, ov, lim), times in groups.items():
            times = sorted(times)
            h, m = times[0]
            run = subprocess.run(
                [str(tmp / "dump_layout"), DATE, "%02d:%02d" % (h, m),
                 "--rounding", str(rnd), "--minutes", str(mins)],
                capture_output=True, text=True)
            draws = [tuple(int(v) for v in l.split())
                     for l in run.stdout.splitlines() if l.strip()]
            img = mock_sweep.render(draws, res, (0, 0, 0), (255, 255, 255),
                                    ring_top, cache)
            img = img.resize((img.width * SCALE, img.height * SCALE),
                             Image.NEAREST)
            slug = lambda t: re.sub(r"[^a-z0-9]+", "", t.lower()) or "x"
            name = "%s-%s-%s-over-%s-%dof%dpx-%02d%02d.png" % (
                ROUNDING[rnd], MINUTES[mins], slug(texts[up]), slug(texts[lo]),
                ov, lim, h, m)
            img.save(OUT / name)
            index.append("\n%s\n  \"%s\" descends %dpx into \"%s\", limit %dpx "
                         "(%d over)\n  reading %s, minutes %s, %d time(s): %s" % (
                             name, texts[up], ov, texts[lo], lim, ov - lim,
                             ROUNDING[rnd], MINUTES[mins], len(times),
                             " ".join("%02d:%02d" % t for t in times)))
            print("  %-58s x%d" % (name, len(times)))

        (OUT / "index.txt").write_text("\n".join(index) + "\n")
        print("\n%d failures -> %d picture(s) in %s/  (see index.txt)"
              % (len(rows), len(groups), OUT.name))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
