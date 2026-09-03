#!/usr/bin/env python3
"""Screenshot the watchface without an emulator.

    python3 tools/mock_sweep.py                # days, dates, times
    python3 tools/mock_sweep.py stacks         # same modes as date_sweep.sh
    python3 tools/mock_sweep.py hours all
    python3 tools/mock_sweep.py 2026-02-29 14:35
    python3 tools/mock_sweep.py stacks --rounding 0 --typeface 1

Writes shots-mock/. Every combination in seconds, deterministically, with no
clock to fight.


WHY THIS EXISTS BESIDE tools/date_sweep.sh
------------------------------------------
The emulator sweep cannot be made completely reliable. emu-set-time does not
redraw the face, so the clock has to be set early and the emulator's own
minute rollover used to trigger the repaint - and any pebble command resets
that clock back to the host's real time. The screenshot is a pebble command.
So there is always a window between the rollover and the shutter in which
the face can revert to the real time, and shots come back showing today's
date instead of the one asked for. Narrowing the window cuts the rate; it
does not reach zero, and the failures move around between runs.

That is tolerable when you need the real display pipeline. It is a bad deal
when the question is only "where does the ink sit", which is most of the
time - and it is exactly the sweeps with the most shots, where the chance of
at least one bad frame is highest.


WHAT THIS IS AND IS NOT
-----------------------
It is NOT a model of the watchface. A second implementation of the layout
would agree on the day it was written and drift silently afterwards, and
every disagreement would be ambiguous: is the watchface wrong, or the model?
A mock-up that is confidently wrong is worse than an emulator screenshot
that is obviously wrong.

So the coordinates come from the shipping code. tools/test/dump_layout.c
includes src/c/handwritten.c and calls the real refresh() and build_face(),
exactly as the test harness does, then prints where every word landed. This
script only pastes the real bitmaps at those coordinates. There is no
layout here to drift.

Faithful, therefore:
  - word placement, to the pixel, from the code that ships
  - which words are chosen, for every reading mode and setting
  - the actual bitmaps, in either typeface, at their real sizes

Approximated:
  - colour. The palette ramp follows tint() in handwritten.c and the level
    encoding in tune.py's to_palette, then rounds to Pebble's two bits per
    channel. Close, but it is this script's arithmetic and not the SDK's.

Absent:
  - the reveal animation, which is a timing question and not a placement one
  - anything the SDK does to a bitmap on the way to the panel

So: use this to judge placement, spacing, and how words stack. Use
tools/sweep.sh on the emulator to sign off on how it actually looks.
"""

import argparse
import calendar
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required:  pip install Pillow --break-system-packages")

ROOT = Path(__file__).resolve().parent.parent
TESTDIR = ROOT / "tools" / "test"
GEOM = ROOT / "src" / "c" / "geometry.h"
CONFIG = ROOT / "src" / "c" / "config.h"
SWEEP = ROOT / "tools" / "date_sweep.sh"
IMGDIR = ROOT / "resources" / "images"
OUT = ROOT / "shots-mock"


def cfg(name, default=None):
    m = re.search(r"^#define %s\s+(\d+)" % name, CONFIG.read_text(), re.M)
    if not m:
        if default is None:
            sys.exit("no #define %s in src/c/config.h" % name)
        return default
    return int(m.group(1))


def resource_map():
    """-> {resource id: image stem}, from the RESOURCE_ID_* defines.

    dump_layout reports what the draw call received, which is a resource id
    and a rectangle - so nothing here needs to know about typefaces, words,
    or which table row was chosen. The id is the answer to all of that.
    """
    ids = re.findall(r"^#define RESOURCE_ID_(\w+)\s+(\d+)",
                     (TESTDIR / "generated.h").read_text(), re.M)
    if not ids:
        sys.exit("no RESOURCE_ID_* in tools/test/generated.h - run tune.py")
    return {int(v): ("h_" + n[2:] if n.startswith("H_") else n).lower()
            for n, v in ids}


# ---------------------------------------------------------------- sweeps
#
# The time and date lists are READ OUT OF tools/date_sweep.sh rather than
# copied here. Two lists that are supposed to be the same list is how the
# two sweeps would quietly stop covering the same ground, and then a mock-up
# would pass what the emulator had never been asked.

def sh_array(name):
    m = re.search(r"^%s=\(\s*(.*?)\)" % name, SWEEP.read_text(),
                  re.M | re.S)
    if not m:
        sys.exit("no %s=( ) array in tools/date_sweep.sh" % name)
    return re.findall(r'"([^"]+)"|(\S+)', m.group(1))


def sh_list(name):
    return [a or b for a, b in sh_array(name)]


def sh_default(name, fallback):
    m = re.search(r'^%s="\$\{%s:-([^}]*)\}"' % (name, name),
                  SWEEP.read_text(), re.M)
    return m.group(1) if m else fallback


def sweep_days():
    ym = sh_default("DAY_MONTH", "2026-08")
    y, mo = (int(v) for v in ym.split("-"))
    hh, mm = int(sh_default("SWEEP_HOUR", "12")), int(sh_default("SWEEP_MINUTE", "28"))
    out = []
    for d in range(1, calendar.monthrange(y, mo)[1] + 1):
        suffix = ("th" if d in (11, 12, 13) else
                  {1: "st", 2: "nd", 3: "rd"}.get(d % 10, "th"))
        out.append(("%04d-%02d-%02d" % (y, mo, d), hh, mm,
                    "day-%02d-%s" % (d, suffix), "%d%s" % (d, suffix)))
    return out


def sweep_dates():
    hh, mm = int(sh_default("SWEEP_HOUR", "12")), int(sh_default("SWEEP_MINUTE", "28"))
    return [(d, hh, mm, "date-%s" % d, d) for d in sh_list("DATES")]


def sweep_times(on):
    out = []
    for entry in sh_list("TIMES"):
        hhmm, tag = entry.split("|")
        hh, mm = (int(v) for v in hhmm.split(":"))
        out.append((on, hh, mm, "time-%s-%s" % (hhmm.replace(":", ""), tag),
                    "%s  %s" % (hhmm, tag)))
    return out


def sweep_stacks(on):
    hour = int(sh_default("STACK_HOUR", "4"))
    out = []
    for mins in range(1, 31):
        out.append((on, hour, mins, "stack-%02d-past" % mins,
                    "%d past" % mins))
        if mins <= 29:
            out.append((on, hour, 60 - mins, "stack-%02d-to" % mins,
                        "%d to" % mins))
    return out


def sweep_hours(on):
    pm = int(sh_default("HOUR_MIN_PAST", "15"))
    tm = int(sh_default("HOUR_MIN_TO", "45"))
    out = []
    for entry in sh_list("HOURS_PAST"):
        hh, idx, word = entry.split("|")
        out.append((on, int(hh), pm, "hour-past-%s-%s" % (idx, word),
                    "past %s" % word))
    for entry in sh_list("HOURS_TO"):
        hh, idx, word = entry.split("|")
        out.append((on, int(hh), tm, "hour-to-%s-%s" % (idx, word),
                    "to %s" % word))
    return out


# ---------------------------------------------------------------- render

def two_bit(v):
    """Pebble has two bits per channel. tint() blends at full precision and
    then rounds to what the panel can show; this does the same, which is
    what puts the visible banding in a stroke's edge."""
    return min(3, max(0, int(round(v / 85.0)))) * 85


def render(draws, res, paper, ink, ring_top, cache):
    w, h = cfg("SCREEN_W"), cfg("SCREEN_H")
    canvas = Image.new("RGB", (w, h), paper)
    px = canvas.load()

    # Mirrors tint(): level 0 is transparent, 1..BOLD_RING_TOP are the
    # emboldening outline (paper unless the ink is the darker of the two),
    # and the rest blend from paper to ink. See tune.py's to_palette for
    # where the levels come from - the PNG's palette INDEX is the level.
    dark_ink = sum(ink) < sum(paper)
    ramp = []
    for lvl in range(16):
        if lvl == 0 or (lvl <= ring_top and not dark_ink):
            ramp.append(None)
            continue
        use = ring_top if (lvl <= ring_top and dark_ink) else lvl
        ramp.append(tuple(
            two_bit((p * (15 - use) + i * use) / 15.0)
            for p, i in zip(paper, ink)))

    for rid, x, top, dw, dh in draws:
        stem = res.get(rid)
        if stem is None:
            sys.exit("draw of unknown resource id %d - "
                     "generated.h and the binary disagree" % rid)
        if stem not in cache:
            im = Image.open(IMGDIR / ("%s.png" % stem))
            if im.mode != "P":
                sys.exit("%s.png is not palettised - re-run tools/tune.py"
                         % stem)
            # tobytes(), not getdata(): mode "P" is one byte per pixel with
            # no row padding, so this is the palette INDEX - which is the
            # level tune.py wrote - and it does not go through the
            # deprecation in newer Pillow.
            cache[stem] = (im.size, im.tobytes())
        (iw, ih), data = cache[stem]
        # The watch REPEATS a bitmap drawn into a rect bigger than itself, so
        # a mismatch here is the smear bug, silently. check_resources.py
        # catches it against geometry.h; this catches it against what was
        # actually asked for, which is one step closer to the screen.
        if (dw, dh) != (iw, ih) and dw >= iw:
            print("  %s: drawn into %dx%d but the image is %dx%d - the watch "
                  "would repeat it" % (stem, dw, dh, iw, ih), file=sys.stderr)
        for row in range(ih):
            yy = top + row
            if yy < 0 or yy >= h:
                continue
            base = row * iw
            for col in range(iw):
                lvl = data[base + col]
                rgb = ramp[lvl] if lvl < 16 else None
                if rgb is None:
                    continue
                xx = x + col
                if 0 <= xx < w:
                    px[xx, yy] = rgb
    return canvas


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("what", nargs="*", default=[],
                    help="days dates times stacks hours all, "
                         "or dates/times by name")
    ap.add_argument("--rounding", type=int, default=None,
                    help="0 exact, 1 nearest five, 2 spoken")
    ap.add_argument("--minutes", type=int, default=None,
                    help="0 auto, 1 never, 2 always")
    ap.add_argument("--format", type=int, default=None, dest="fmt")
    ap.add_argument("--typeface", type=int, default=None,
                    help="0 the font, 1 the handwriting")
    ap.add_argument("--scale", type=int, default=1,
                    help="write the images this many times larger")
    args = ap.parse_args()

    # Build the dumper from the shipping source. Not cached between runs:
    # it is a second of gcc, and a stale binary here would be a mock-up of
    # a layout that no longer exists - the exact failure this project keeps
    # hitting with generated files.
    tmp = Path(tempfile.mkdtemp())
    try:
        exe = tmp / "dump_layout"
        cc = subprocess.run(
            ["gcc", "-std=c11", "-I.", "-Wall", "-Wno-unused-function",
             "-Wno-unused-parameter", "-O1", "-o", str(exe),
             "dump_layout.c", "stub.c"],
            cwd=TESTDIR, capture_output=True, text=True)
        if cc.returncode:
            sys.exit("could not build tools/test/dump_layout.c:\n" + cc.stderr)

        on = sh_default("SWEEP_DATE", "2026-08-22")
        want = [a for a in args.what]
        named_d = [a for a in want if re.fullmatch(r"\d{4}-\d\d-\d\d", a)]
        named_t = [a for a in want if re.fullmatch(r"\d\d?:\d\d", a)]
        modes = [a for a in want if a not in named_d and a not in named_t]
        if "all" in modes:
            modes = ["days", "dates", "times", "stacks", "hours"]
        if not modes and not named_d and not named_t:
            modes = ["days", "dates", "times"]

        shots = []
        if "days" in modes:
            shots += sweep_days()
        if "dates" in modes:
            shots += sweep_dates()
        if "times" in modes:
            shots += sweep_times(on)
        if "stacks" in modes:
            shots += sweep_stacks(on)
        if "hours" in modes:
            shots += sweep_hours(on)
        for d in named_d:
            shots.append((d, int(sh_default("SWEEP_HOUR", "12")),
                          int(sh_default("SWEEP_MINUTE", "28")),
                          "date-%s" % d, d))
        for t in named_t:
            hh, mm = (int(v) for v in t.split(":"))
            shots.append((on, hh, mm,
                          "time-%s-custom" % t.replace(":", ""), t))

        res = resource_map()
        ring_top = cfg("BOLD_RING_TOP", 2)
        paper, ink = (0, 0, 0), (255, 255, 255)
        cache = {}

        OUT.mkdir(exist_ok=True)
        # Anything not asked for is left to settings_defaults() in the C,
        # rather than restated here where it could disagree with it.
        opts = []
        for flag, val in (("--rounding", args.rounding),
                          ("--minutes", args.minutes),
                          ("--format", args.fmt),
                          ("--typeface", args.typeface)):
            if val is not None:
                opts += [flag, str(val)]

        print("%d shots -> %s/%s" % (len(shots), OUT.name,
                                     ("  " + " ".join(opts)) if opts else ""))
        for date, hh, mm, name, label in shots:
            run = subprocess.run(
                [str(exe), date, "%02d:%02d" % (hh, mm)] + opts,
                capture_output=True, text=True)
            if run.returncode:
                sys.exit("dump_layout failed on %s %02d:%02d:\n%s"
                         % (date, hh, mm, run.stderr))
            draws = [tuple(int(v) for v in line.split())
                     for line in run.stdout.splitlines() if line.strip()]
            img = render(draws, res, paper, ink, ring_top, cache)
            if args.scale > 1:
                img = img.resize((img.width * args.scale,
                                  img.height * args.scale), Image.NEAREST)
            img.save(OUT / ("%s.png" % name))
            print("  %-28s -> %s/%s.png" % (label, OUT.name, name))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print("\nPlacement is the shipping build_face(), to the pixel. Colour is\n"
          "this script's arithmetic, and there is no reveal animation - for\n"
          "those, tools/sweep.sh on the emulator.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
