#!/usr/bin/env python3
"""What would changing these config.h constants actually change?

    python3 tools/preview_offset.py OFFSET_MINUTE_SPLIT=3 OFFSET_MINUTES=3

Writes shots-offset-preview/: one before | after | change picture for every
distinct face the change touches, and an index.txt with the numbers. Your
config.h is not modified - both versions are built in a temporary directory.


HOW "AFFECTED" IS DECIDED
-------------------------
Not by reasoning about which rows an offset belongs to. The test harness is
built twice, once with the current config.h and once with the change, and
both sweep every minute of the day under every combination of reading mode,
minutes setting and date format, logging every pair of stacked words and how
far one reaches into the other. Any pair whose spacing differs between the
two runs is affected. That covers cases nobody would think to look at, and
it cannot disagree with the watchface, because it IS the watchface's layout
code.

The pictures come from the same place as the mock sweep: dump_layout.c runs
the shipping draw path and reports where every word went, and the real
bitmaps are pasted there.

The third panel overlays the two: ink only in "before" is red, ink only in
"after" is cyan, ink in both is grey. A one-pixel move shows up as a red and
a cyan fringe, which is otherwise invisible side by side.

ONE BLIND SPOT, stated plainly: the harness does not measure the inline
"minute(s)" beside a split number against its neighbours above and below -
it shares the number's baseline rather than stacking, so the interweave
check skips it. If the change moves that word, the pictures are the only
evidence for it. Look.
"""

import collections
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mock_sweep                               # noqa: E402

from PIL import Image, ImageDraw                # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "shots-offset-preview"
DATE = "2026-08-21"        # the date the harness sweeps (make_tm 232, 21, 7)
SCALE = 3
ROUNDING = {0: "exact", 1: "five", 2: "spoken"}
MINUTES = {0: "auto", 1: "never", 2: "always"}


def parse_changes(args):
    changes = {}
    for a in args:
        m = re.fullmatch(r"([A-Z][A-Z0-9_]*)=(-?\d+)", a)
        if not m:
            sys.exit("expected NAME=value, e.g. OFFSET_MINUTES=3 - got %r" % a)
        changes[m.group(1)] = int(m.group(2))
    if not changes:
        sys.exit(__doc__.strip().split("\n\n")[0])
    return changes


def make_tree(dst, changes):
    """A copy of the two directories the C builds need, with the change."""
    (dst / "src").mkdir(parents=True)
    (dst / "tools").mkdir()
    shutil.copytree(ROOT / "src" / "c", dst / "src" / "c")
    shutil.copytree(ROOT / "tools" / "test", dst / "tools" / "test")
    cfg = dst / "src" / "c" / "config.h"
    text = cfg.read_text()
    for name, val in changes.items():
        pat = re.compile(r"^(#define %s\s+)(-?\d+)" % name, re.M)
        m = pat.search(text)
        if not m:
            sys.exit("no '#define %s <number>' in src/c/config.h" % name)
        text = pat.sub(lambda mm: mm.group(1) + str(val), text, count=1)
    cfg.write_text(text)


def build_and_sweep(tree):
    test = tree / "tools" / "test"
    flags = ["gcc", "-std=c11", "-I.", "-w", "-O1"]
    for exe, src in (("harness", "harness.c"), ("dump_layout", "dump_layout.c")):
        cc = subprocess.run(flags + ["-o", exe, src, "stub.c"], cwd=test,
                            capture_output=True, text=True)
        if cc.returncode:
            sys.exit("could not build %s:\n%s" % (src, cc.stderr))
    log = tree / "pairs.txt"
    subprocess.run([str(test / "harness")], cwd=tree, capture_output=True,
                   env={"HARNESS_PAIRS_LOG": str(log), "PATH": "/usr/bin:/bin"})
    pairs = {}
    for line in log.read_text().splitlines():
        h, m, rnd, mins, fmt, up, lo, ov, lim = map(int, line.split())
        pairs[(h, m, rnd, mins, fmt, up, lo)] = (ov, lim)
    return pairs, test / "dump_layout"


def dump(exe, h, m, rnd, mins):
    out = subprocess.run([str(exe), DATE, "%02d:%02d" % (h, m),
                          "--rounding", str(rnd), "--minutes", str(mins)],
                         capture_output=True, text=True).stdout
    return [tuple(int(v) for v in l.split()) for l in out.splitlines() if l.strip()]


def overlay(a, b):
    """Red = only in before, cyan = only in after, grey = both."""
    a, b = a.convert("L"), b.convert("L")
    out = Image.new("RGB", a.size)
    pa, pb, po = a.load(), b.load(), out.load()
    for y in range(a.height):
        for x in range(a.width):
            va, vb = pa[x, y] > 60, pb[x, y] > 60
            po[x, y] = ((110, 110, 110) if va and vb else
                        (255, 60, 60) if va else
                        (60, 230, 255) if vb else (0, 0, 0))
    return out


def main():
    changes = parse_changes(sys.argv[1:])
    texts = mock_sweep_texts()
    tmp = Path(tempfile.mkdtemp())
    try:
        make_tree(tmp / "before", {})
        make_tree(tmp / "after", changes)
        print("sweeping the current config.h and the changed one ...")
        before, dl_before = build_and_sweep(tmp / "before")
        after, dl_after = build_and_sweep(tmp / "after")

        if set(before) != set(after):
            print("note: the change altered WHICH words stack, not just where "
                  "- %d pair(s) differ in kind" % len(set(before) ^ set(after)))
        moved = {k for k in set(before) & set(after) if before[k] != after[k]}

        # Spacing, summarised per kind of pair. "Room" is how far the upper
        # word could still descend before hitting the limit; negative means
        # it is already over.
        per = collections.defaultdict(lambda: [None, None, None, None, 0])
        for k in moved:
            up, lo = k[5], k[6]
            ob, lim = before[k]
            oa, _ = after[k]
            r = per[(up, lo)]
            r[0] = ob if r[0] is None else max(r[0], ob)   # worst before
            r[1] = oa if r[1] is None else max(r[1], oa)   # worst after
            r[2] = lim
            r[3] = oa - ob
            r[4] += 1
        newly_failing = sum(1 for k in moved
                            if after[k][0] > after[k][1] >= before[k][0])
        fixed = sum(1 for k in moved
                    if before[k][0] > before[k][1] >= after[k][0])

        groups = collections.OrderedDict()
        for k in sorted(moved, key=lambda k: (k[2], k[3], k[1], k[0])):
            groups.setdefault((k[2], k[3], k[1]), k[0])

        if OUT.exists():
            shutil.rmtree(OUT)
        OUT.mkdir()

        res = mock_sweep.resource_map()
        ring_top = mock_sweep.cfg("BOLD_RING_TOP", 2)
        cache, seen, written = {}, set(), 0
        for (rnd, mins, m), h in groups.items():
            db, da = dump(dl_before, h, m, rnd, mins), dump(dl_after, h, m, rnd, mins)
            if (tuple(db), tuple(da)) in seen or db == da:
                continue
            seen.add((tuple(db), tuple(da)))
            ib = mock_sweep.render(db, res, (0, 0, 0), (255, 255, 255), ring_top, cache)
            ia = mock_sweep.render(da, res, (0, 0, 0), (255, 255, 255), ring_top, cache)
            panels = [ib, ia, overlay(ib, ia)]
            w, hh = ib.width * SCALE, ib.height * SCALE
            sheet = Image.new("RGB", (w * 3 + 40, hh + 30), (40, 40, 40))
            draw = ImageDraw.Draw(sheet)
            for i, (img, label) in enumerate(zip(panels,
                                                 ("before", "after", "change"))):
                sheet.paste(img.resize((w, hh), Image.NEAREST), (10 + i * (w + 10), 25))
                draw.text((10 + i * (w + 10), 6), label, fill=(220, 220, 220))
            side = "past" if m <= 30 else "to"
            name = "%s-%s-%02d%02d-%s.png" % (ROUNDING[rnd], MINUTES[mins], h, m, side)
            sheet.save(OUT / name)
            written += 1

        lines = ["Change: " + "  ".join("%s=%d" % kv for kv in changes.items()),
                 "",
                 "%d stacked-pair measurements changed, across every minute, "
                 "reading mode,\nminutes setting and date format. %d picture(s) "
                 "cover every distinct face.\n" % (len(moved), written),
                 "Failures fixed: %d   newly failing: %d\n" % (fixed, newly_failing),
                 "Spacing by pair. Overlap = how far the upper word's ink reaches "
                 "into the\nlower one's (negative = a gap). Limit = what the test "
                 "allows.\n",
                 "  %-26s %10s %10s %7s %6s" % ("upper over lower", "before",
                                                "after", "limit", "cases")]
        for (up, lo), (ob, oa, lim, d, n) in sorted(per.items(),
                                                    key=lambda kv: -kv[1][1]):
            lines.append("  %-26s %8dpx %8dpx %5dpx %6d%s" % (
                "%s over %s" % (texts[up], texts[lo]), ob, oa, lim, n,
                "   OVER LIMIT" if oa > lim else ""))
        lines += ["", "Not measured by the harness: the inline \"minute(s)\" beside "
                  "a split number.", "Judge that one from the pictures."]
        (OUT / "index.txt").write_text("\n".join(lines) + "\n")
        print("\n".join(lines))
        print("\n-> %s/" % OUT.name)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return 0


def mock_sweep_texts():
    src = (ROOT / "tools" / "test" / "generated.h").read_text()
    block = src.split("static const char *const WORD_TEXT[] = {")[1]
    return re.findall(r'^\s*"(.*)",\s*$', block.split("};")[0], re.M)


if __name__ == "__main__":
    sys.exit(main())
