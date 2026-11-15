# Handwritten (British) — Build Guide

Confirmed working end-to-end: builds clean, runs in the emulator, installs
and displays correctly on a real Pebble Time 2 over Dev Connect. This is the
guide as it should have been written the first time — every genuine gotcha
folded in, false starts removed.

`CONTEXT.md` explains *why* the code works the way it does. This is just
*how* to build and run it.

---

## 1. Install the SDK toolchain (Ubuntu / WSL2)

**If you're on Windows, this must be WSL2, not WSL1.** WSL1 cannot show the
emulator window at all — no Linux GUI support exists in that mode, and no
amount of updating fixes it. Check with (in PowerShell):
```powershell
wsl -l -v
```
If it shows `1` next to your distro, convert it:
```powershell
wsl --set-version Ubuntu 2
```
This can take several minutes; let it finish undisturbed.

**Update package lists before installing anything** — a fresh WSL install
has no populated package index, which otherwise shows up as confusing
`Unable to locate package` errors on packages that do exist:
```bash
sudo apt update
sudo apt install nodejs npm libsdl2-2.0-0 libglib2.0-0 libpixman-1-0 zlib1g libsndio7.0
```

**If `nodejs`/`npm` end up broken** — symptom is `/usr/bin/node: 1: Syntax
error: "(" unexpected` later during SDK install, which means the shell is
trying to run a JS file as a script because `node` isn't a real binary.
Ubuntu's bundled Node/npm pairing is known to break this way. Replace it
with an official build:
```bash
sudo apt remove --purge nodejs npm -y
sudo apt install -y ca-certificates curl gnupg
sudo mkdir -p /etc/apt/keyrings
curl -fsSL https://deb.nodesource.com/gpgkey/nodesource-repo.gpg.key | sudo gpg --dearmor -o /etc/apt/keyrings/nodesource.gpg
echo "deb [signed-by=/etc/apt/keyrings/nodesource.gpg] https://deb.nodesource.com/node_22.x nodistro main" | sudo tee /etc/apt/sources.list.d/nodesource.list
sudo apt update
sudo apt install nodejs -y
node -v && npm -v     # both should print clean version numbers
```

---

## 2. Install `pebble-tool`

The standard route uses `uv`:
```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```
**Open a new terminal, or `source ~/.bashrc`**, before continuing — the
installer edits your shell profile, which only takes effect in a fresh
shell. Then:
```bash
uv tool install pebble-tool
```

**If that fails with `Cannot allocate memory (os error 12)`** — including
after trying `UV_LINK_MODE=copy` — this is a real, unresolved WSL-specific
issue with `uv`'s installer, not something wrong with your machine. Skip
`uv` for this one package and use `pipx` instead, which has been confirmed
to work when `uv` fails outright:
```bash
sudo apt install pipx
pipx ensurepath
```
Open a new terminal again, then:
```bash
pipx install pebble-tool
```
From here on, every `pebble` command works identically regardless of which
route installed it.

Then the SDK itself:
```bash
pebble sdk install latest
```

### Updating later

Two different things prompt you to update, and they are fixed in two
different places. Both have cost time once already by being mistaken for
each other.

**"A newer version of pebble-tool is available"** — that is the CLI.
`uv tool upgrade pebble-tool` is the command the prompt suggests, and on
this machine it answers:

```
error: Failed to upgrade pebble-tool
  Caused by: `pebble-tool` is not installed
```

which is correct and not a fault. `uv` never installed it — `uv tool
install` failed with `Cannot allocate memory (os error 12)` above, so it
went in through **pipx**. Upgrade it the way it was installed:

```bash
pipx upgrade pebble-tool
```

`which pebble` says which route is in play: `~/.local/bin/pebble` is pipx's
shim, `~/.local/share/uv/tools/...` would be uv's.

**"A new SDK is available: v4.33.1"** — that is the SDK, not the CLI, and
`pebble sdk install latest` can answer that you already have the latest
while the prompt keeps appearing. Name the version and activate it:

```bash
pebble sdk install 4.33.1
pebble sdk activate 4.33.1
```

---

## 3. Get the project onto disk

```bash
cd ~
pebble new-project handwritten-british
cd handwritten-british
```

Copy in, from this chat's deliverables, replacing the generated versions:
```
package.json
src/c/config.h
src/c/handwritten.c
src/pkjs/config.js
src/pkjs/index.js
tools/tune.py
resources/fonts/watchface.ttf
```
Delete the generated `src/c/*.c` stub first, so there's only one `main`.

**On Windows:** the project lives inside WSL's own filesystem
(`/home/<you>/handwritten-british`), not on the `C:` drive — cross-boundary
file access is slow and has caused build oddities before. To drag files in
from Windows without moving the project itself, run this from inside the
project folder:
```bash
explorer.exe .
```
That opens the same folder in a normal Windows Explorer window.

**Clean up Windows' download markers**, if you dragged files in — harmless,
but worth clearing so nothing confuses a tool later:
```bash
find . -name '*:Zone.Identifier' -delete
```

Install Clay, used for the on-watch settings page:
```bash
pebble package install pebble-clay
```

---

## 4. Generate the word images

```bash
pip install Pillow --break-system-packages   # if not already present
python3 tools/tune.py
```

This renders all 83 words the face can show into `resources/images/`,
writes `src/c/geometry.h` and `tools/test/generated.h`, and updates the
bitmap resource list in `package.json`. **Re-run this any time you change a
`FONT_SIZE_*` in `config.h`** — image, geometry, and manifest all have to
move together, or the layout math and the actual pictures on screen will
disagree. **Also re-run it after adding any word** — a new date format, a
new wording rule — since the word list lives in `tune.py`.

`handwritten.c` will refuse to compile against a stale `geometry.h` rather
than build something wrong. Each guard names the generation it needs:

```
#error "src/c/geometry.h predates uniform family boxes. Run: python3 tools/tune.py"
#error "src/c/geometry.h has no weekday words. Run: python3 tools/tune.py"
```

Both mean the same thing — run `tune.py`. They exist because a stale
`geometry.h` once *did* get committed on its own, and the mismatch between
the table and the images it indexes is not something a build would
otherwise notice.

It also writes `handwriting-templates/` — the same 83 words as plain PNGs
at their exact final sizes, for tracing over with real handwriting later.

`tune.py` checks the vertical budget before it writes anything, and stops
if the tallest phrase no longer fits, naming the constant to change:

```
DOES NOT FIT:
  - top row is clipped by 6px (lower REL_TOP or ROW_GAP)
  - hour row collides with the date by 7px (raise DATE_BASELINE, ...)
```

Raising `FONT_SIZE_TIME` or `ROW_GAP` is what usually trips it. See
`CONTEXT.md` §3.2 for how tight the budget is and why.

---

## 5. Run the layout tests

```bash
tools/test/run.sh
```

Needs `gcc` and nothing else — no Pebble SDK, so this runs anywhere and
takes under a second. If `node` happens to be installed it also runs the
settings-page test (`clay-slider.test.js`); if not, it says so and carries
on. It compiles the real `handwritten.c` against a stub of
the SDK and sweeps every minute of the day and every date of a leap year,
once per date format, minutes mode and reading mode — about 3.2 million
assertions, under
AddressSanitizer.

Run it before every `pebble build`. It catches in a second what otherwise
takes an emulator round trip to notice, and several of the bugs in
`CONTEXT.md` §6 are in it as explicit regression tests.

If it reports `tools/test/generated.h is missing`, run `tools/tune.py` first
(§4). On Windows, run it from WSL like everything else here.

---

## 6. Build and test in the emulator

```bash
pebble build
pebble install --emulator emery
```

Worth checking a few specific times, since these exercise the trickiest
parts of the layout at once:
```bash
pebble emu-set-time --emulator emery 00:28:00   # worst-case 4-row split phrase
pebble emu-set-time --emulator emery 23:40:00   # regression check: must show "twenty", not "one"
pebble emu-set-time --emulator emery 03:00:00   # the witching-hour easter egg
pebble emu-set-time --emulator emery 12:00:00   # solo "midday"
```
`pebble logs --emulator emery` shows live log output if anything looks off,
including anything `src/pkjs/` throws or logs.

**If `install` reports `App install failed.`** and the emulator's progress
bar stalls about halfway, the emulator's stored flash image is corrupt:

```bash
pebble kill
pebble wipe
```

Nothing else fixes it — not rebuilding, not `pebble clean`, not restarting
the computer, because the bad state lives in the emulator's own storage
rather than in the build or the process. The error message is unhelpfully
generic and there is no `--debug` flag to prise more out of it, so reach for
`wipe` early rather than late.

The quickest way to tell whether a failure is yours or the emulator's is to
build a throwaway project and install that:

```bash
cd /tmp && pebble new-project hello && cd hello
pebble build && pebble install --emulator emery
```

If hello-world fails too, stop looking at your own code.

Note that `wipe` also clears any settings saved into the emulator, so the
upgrade check in §6.1 starts from scratch afterwards.

### 6.1 The settings page, without a phone

```bash
pebble emu-app-config --emulator emery
```

The app has to be installed and running in the emulator first. This fires
the same `showConfiguration` event the phone sends; Clay builds the page and
pebble-tool opens it in your desktop browser. Saving navigates to
`pebblejs://close#…`, which pebble-tool intercepts and delivers to the
watchface as an AppMessage — the identical path to the phone, so
`tuple_int()`, the clamps and the persistence are all exercised for real.

`pebble emu-app-config --help` lists the extra flags, including pointing at
a local page instead of the generated one.

Two things this catches better than a phone does:

- **The mouse path through `src/pkjs/custom-clay.js`.** The drag-only slider
  fix handles `pointerdown`, `touchstart` and `mousedown`, and a watch only
  ever exercises touch — a desktop browser is the only place the
  `mousedown` branch runs at all. Click the middle of a slider track and
  confirm nothing moves, then drag the dot.
- **The settings upgrade path.** `Settings` has been appended to four times
  (`date_format`, `minutes_mode`, `stroke_weight`, `offset_split_head`),
  each relying on `persist_read_data` filling a short blob and leaving the
  rest at their defaults. Install an older build, set some colours and
  offsets, install the current one, and check they survived with the new
  fields at defaults. If the append-only discipline ever slips, this is how
  you find out — and it is far more awkward to do on the watch.

What it will **not** tell you is anything about the panel. The emulator
renders an idealised display: no bloom on light-on-dark, no thinning on
dark-on-light, so the stroke-weight work of §2 is invisible there and the
four weights will look like four rather than the three-and-a-bit the real
screen collapses them to. That stays a hardware question.

---

## 7. Install on the real watch

**One-time setup, on your phone:**
1. Install the Pebble app from **repebble.com/app** — not necessarily the
   Play Store build, which can lag behind.
2. Pair your watch under the **Devices** tab.
3. Still on that screen, tap the **⋮** (three dots) — **Enable Dev
   Connect** — sign in with GitHub.

**On the computer**, log in with the same GitHub account:
```bash
pebble login
```
If your environment can't open a browser automatically (common on WSL),
the command prints a URL directly in the terminal — copy it into any
browser by hand, complete the sign-in there, then return to the terminal.

Then, any time you want to push a build to the actual watch:
```bash
pebble build
pebble install --cloudpebble
```
This routes through a cloud relay — your phone and computer don't need to
share a network. `pebble logs --cloudpebble` gives you live logs from the
real hardware the same way `--emulator` does from the simulator.

---

## 7b. Handwriting

**A build carries one typeface: the handwriting, or the font.**

```
python3 tools/tune.py            # the handwriting (default)
python3 tools/tune.py --font     # the rendered font instead
```

They used to ship side by side with a **Lettering** switch, so the
handwriting could be compared on the wrist while it was drawn a family at a
time. Both together came to 263 kB of resources against a 256 kB publishing
limit; one alone is about 132 kB. Once every word was drawn, the comparison
stopped earning its keep, and the switch went.

`tune.py` renders the font on **every** run either way, and has to: the
family boxes are measured from its ink, `extract_handwriting.py` cuts the
drawn pages to those boxes, and the pages you draw over **are** its images.
`--font` only changes what gets packaged and what `geometry.h` points at.
Heights and baselines come from the family box in both, so the layout is
identical — only the word widths differ.

**Without `--font`, every word must be drawn.** There is no per-word
fallback between the two, and `tune.py` refuses to write a `geometry.h` with
a hole in it — naming the missing words and suggesting `--font`.

**With `--font`, the handwriting is not read at all.** A fresh clone with no
ink builds fine, and so does anyone who swaps in a different TTF at
`resources/fonts/watchface.ttf` and wants to see it before drawing a thing.
That is the reason the font stays in the repo.

`--font` is not remembered. Each `tune.py` run picks one, so run it the same
way every time while you're working in that mode. `tools/sync.sh` reads which
one the last run chose from `geometry.h` and checks the packaged images
match.

The loop:

```
python3 tools/make_templates.py TIME     # the pages to draw on
                                         # draw them on the Nomad, export PDF
                                         # drop it in handwriting-templates/drawn/
python3 tools/extract_handwriting.py     # -> resources/images/h_*.png
python3 tools/tune.py                    # -> geometry.h, package.json
tools/test/run.sh
```

`extract_handwriting.py` touches no font, so its output is identical on any
machine and anyone can run it. `tune.py` still must not be - see section 2.

Each template page carries its family and page number as a row of squares in
the top corner, so an export can be renamed, split or merged freely. Pages
drawn before that existed fall back to reading the family out of the
filename, and say so.

## 8. Tuning without a rebuild

Open the watchface's settings in the Pebble phone app. Fourteen sliders
nudge a row up (negative) or down (positive) by up to 15px, and apply the
moment you change them. No rebuild, no reinstall. The same page carries the
colours, the date on/off toggle, the date format, whether `minute(s)` is
spoken, and the stroke weight.

**Zero is the tuned layout, not a flat one.** The offsets in
`src/c/config.h` are baked into the design the same way `REL_TOP` is, and a
slider carries a *deviation* from that. So a slider put back to 0 restores
the design rather than undoing it, and there is no need to remember the
number you started from.

That also means the tuning is written down once. `src/pkjs/config.js`
declares every slider default as 0, which is now simply true rather than a
second copy of the same numbers that had to be kept in step by hand.

If you tune on the watch and want to keep it, copy the slider values into
the matching `OFFSET_*` in `config.h`, **add them to what is already
there**, and set the sliders back to 0.

**Stroke weight** only does anything when the ink is darker than the paper.
Dark strokes render about 20% thinner than light ones on this screen, so
levels 1–2 of every word image carry a slightly bolder outline that gets
inked in that direction only — `CONTEXT.md` §2 has the measurements. Four
choices, because four levels per channel is all the screen has: Off, Light,
Medium (the measured match, and the default) and Solid.

**Anything that cannot affect what is on the watch is greyed out.** Which
rows get drawn depends on the reading mode, the "minutes" wording and the
date toggle: rounding to the nearest five leaves 25 as the only minute in
the twenties, so nothing splits and the two split sliders are dead; the
hedge exists only in the spoken mode; with the date off, both the date
slider and the date format are for something nobody can see. A control that
does nothing is worse than a missing one, because the reasonable conclusion
on moving it and seeing nothing happen is that the watchface is broken.

`src/pkjs/custom-clay.js` drives this off all three controls and updates
live, so it tracks the choice without a save-and-reopen. The table it uses
is **not hand-written**: `tools/test/harness.c` sweeps every combination
across all 1440 minutes, records which rows the layout actually draws, and
writes `tools/test/reachability.json`; `tools/test/clay-slider.test.js` then
drives the real handler across the same combinations and fails if the page
greys out anything different. Change what a mode draws and the settings page
has to be revisited — it cannot quietly go stale.

It changes how solidly the outline is inked, **not how far it extends** —
the outer edge of the outline sets the apparent width either way, so Solid
reads harder-edged rather than thicker. To make the strokes genuinely
thicker, raise `BOLD_BLEND` in `tools/tune.py` and regenerate.

Four of those sliders cover the rows above *past*/*to*, and they are
separate on purpose:

| slider | moves |
|---|---|
| Whole phrase, with "just gone" / "nearly" | the minute, "past"/"to" and the hour together — not the hedge, date or on-the-hour wording. **Spoken mode only**, where the phrase sits lower to leave the hedge its room |
| Whole phrase, without the hedge | the same phrase in plain "nearest five", where nothing sits above it and it can ride higher. **That mode only** |
| "just gone" / "nearly" | the hedge above the stacked phrase, spoken mode only |
| "just gone" / "nearly", on the hour | the same word above the o'clock wording. It hangs above the wording rather than sharing the space with it, so the hour never shifts as the hedge comes and goes |

When the clock reaches a round five the hedge stops applying, and because
:04 and :05 read the same the rest of the face does not change — so the word
used to blink out between frames. It is now **rubbed out right to left**, at
the same speed the reveal writes, taking about a quarter of a second for
*nearly* and a little under four tenths for *just gone*. Only the hedge does
this: everywhere else a departing word is covered by an arriving one.
|---|---|
| Split number, top half | only `twenty-`, so it can be pulled clear of the screen edge on its own |
| Minute number, with "minute(s)" below | :01–:20 when the annotation shows and fills the gap |
| Minute number, alone | `quarter`, `half`, any unannotated number — nothing under it |
| Split number, lower half | the `one` of `twenty-one`, annotated or not |
| "minute(s)" beside the number | :21–:29, riding next to the split |
| "minute(s)" on its own line | :01–:20, where nothing else holds it |

The two "whole phrase" sliders are separate because the modes want the
block in two different places, and one lever would mean every adjustment to
either mode dragged the other with it. What they must NOT do is change the
phrase's internal spacing: the harness asserts the two rounded modes differ
by exactly one rigid translation, so if tuning one of these ever makes a row
slide relative to its neighbours, that is a bug and the suite will say so.

Keep **"minute(s)" beside the number** at the same value as **Split number,
lower half** — that is the row it actually sits beside — and the two stay
level. The standalone one is unconstrained, so it is purely how close it
sits to *past*/*to*.

For anything beyond a pixel nudge — spacing between two specific rows, or
how large a word's ink reads relative to its neighbours — edit that word's
PNG directly in `resources/images/` (padding only ever *increases* a gap,
since canvases are cropped tight and stacked) and re-run `tools/tune.py`.

---

## 9. If you (or an AI assistant) hand-edit `package.json`

**Never regenerate this file wholesale.** Two fields are easy to silently
destroy that way, and neither failure is obvious until you go looking for
something specific much later:

- **`dependencies`** — `pebble package install <name>` (e.g. `pebble-clay`)
  writes an entry here. A full-file rewrite that doesn't carry this forward
  leaves the build *looking* fine — `node_modules/` is still on disk from
  the original install, so `pebble build` keeps working — right up until a
  clean checkout or a `node_modules` wipe, at which point the app silently
  loses its settings page (or whatever the dropped package provided) with
  no error pointing at the cause.
- **`pebble.capabilities`** — must contain `"configurable"` or the phone
  app has no way to know a settings page exists, no matter how correctly
  Clay itself is wired up on both sides. This one doesn't even need
  `node_modules` to mask it — it fails silently and permanently until
  someone thinks to check for it specifically, which is exactly what
  happened during this project's own development.

If you need to change `package.json` by hand, edit the specific key you
mean to change and leave everything else untouched — or use
`pebble package install` for anything dependency-related, which manages
this correctly on its own.

A third field is worth the same care: **`pebble.messageKeys`**. The SDK
numbers these by list position, so a new key goes on the **end**. Both
sides are regenerated from this file at build time, so renumbering is
internally consistent and will not break anything today — but appending
keeps the C stub in `tools/test/pebble.h`, which hard-codes the numbers,
honest without a hunt.

---

## 10. Adding a date format

Three edits and a `tune.py` run. Formats are data — `build_date()` should
not need to change.

1. **New words?** If the format shows something the face has never drawn
   (the weekday words were exactly this), add them to `tools/tune.py`'s
   word list, appended at the **end** so existing resource ids don't
   renumber. Anything in the `DATE` family that fits inside the existing
   box costs nothing: the other date PNGs regenerate byte-identically.
2. **`src/c/handwritten.c`** — add a row to `kDateFormats[]`, on the end.
   The index is the wire format; the phone stores and sends it, so
   reordering existing rows changes the date under everyone who already
   chose one.
3. **`src/pkjs/config.js`** — add the matching `options` entry, with the
   same index as its `value`.

Then `python3 tools/tune.py && tools/test/run.sh`. The test sweep is driven
off `DATE_FORMAT_COUNT`, so the new format is swept for bounds, baseline
and centring automatically — but the format-*specific* assertions are not
written for you. Add one, and prove it can fail before believing it.

---

## 11. Checking every date

The date line is the one part of the layout that changes shape daily, and
the handwriting is newest there. The harness already proves every date
**fits** — it sweeps all 366 in both formats and reports the widest line:

```
dates swept          366
widest date line     format 0: 141px on 22/08, 59px of the screen to spare
widest date line     format 1: 138px on 22/08, 62px of the screen to spare
```

Format 1 is checked against all seven weekdays for every date, not just the
pairing a given year happens to produce, so that figure is the true worst
case rather than one year's.

With 59px spare there is no crowding risk, so what is left is **aesthetic** —
does the handwriting read well — and **22/08 is where to start**, not a walk
through all 366.

Run the sweep:

```
tools/date_sweep.sh          # days, dates, then times — 66 shots, ~4 min
tools/date_sweep.sh days     # every day number, 1st to 31st
tools/date_sweep.sh dates    # one date per month
tools/date_sweep.sh times    # just the phrase
tools/date_sweep.sh stacks   # every word that can sit ABOVE past/to — 59
tools/date_sweep.sh hours    # every word that can sit BELOW past/to — 26
tools/date_sweep.sh all      # all five — 151 shots, ~10 min
tools/date_sweep.sh 2026-02-29 14:35   # anything you name
```

It writes `shots/`. Five sets, each isolating one variable:

**days** — all 31 day numbers in one month. Each digit is its own bitmap, so
a two-digit day sets two side by side and the *pairing* is a thing in its own
right. There is no clever subset: days 10–31 **are** the twenty-two possible
pairs, and 1–9 are the single-digit cases. All in August, so the only thing
changing is the number, and August is the widest month word — every pairing
is seen in the tightest line it will appear in.

**dates** — twelve dates covering every month, weekday and ordinal form,
including the 11th–13th exception.

**times** — twenty-three times covering every distinct layout rather than
every distinct wording: centred vs stacked, split vs whole, with and without
the annotation, both sides of the hour, and the rollovers into midnight and
midday. Pinned to 22 August so the widest date line sits under every phrase.

**stacks** and **hours** answer a different question, and are opt-in for that
reason. The three sets above ask *does it fit*, for which one shot per layout
is the right unit. These ask *does it look right*, and what looks wrong is a
particular descender landing on a particular ascender — a property of the two
words, not of the layout. "past" under "quarter" and "past" under "twenty"
are the same layout and a completely different picture.

`past` and `to` are where this bites. Every phrase has one, they are the only
words with a row above *and* a row below, and their ink is nearly all
x-height with one deep descender and one tall ascender.

**stacks** — every word that can sit directly above them. Which word that is
depends on the reading mode and the minutes setting, so no fixed list of
times covers it; but it is always a function of the minute alone, and minute
*m* puts its word above "past" while minute *60−m* puts the same word above
"to". So all 59 minutes is the complete set of both, whatever the settings.
Shots are named by the **spoken** minute, so `stack-08-past` and
`stack-08-to` sit next to each other — an offset that fixes one and breaks
the other is the trap. **Run this one in EXACT mode**: the rounded modes
collapse most of the 59 onto a dozen phrases and never split 21–29.

**hours** — every word that can sit directly below them. Fourteen hour words,
and they are not symmetric: "twelve" only ever appears under "to", "noon"
only under "past", "midnight" under both, so each side is swept in full.
Pinned to :15 and :45, which survive every reading mode unchanged and never
take a hedge — so this is the one sweep that looks identical in all three
modes, with the phrase above held still while the word below changes.

The **year** is not swept — four fixed digits that change once a year. If a
later year looks wrong, name a date in it.

Do it once per **reading mode**. The
phrase shapes genuinely differ between the three modes, and three of the
times only show a hedge in "nearest five, spoken". Compare the sets side by
side; judging one screenshot at a time hides exactly the inconsistency you
are looking for.

### Sweeping without an emulator: `tools/mock_sweep.py`

```
python3 tools/mock_sweep.py stacks          # same modes as date_sweep.sh
python3 tools/mock_sweep.py 2026-02-29 14:35 --scale 3
```

Writes `shots-mock/`. **59 shots in under two seconds**, deterministically,
with no clock to fight — against roughly five minutes on the emulator with a
few frames showing the real time.

Use it for placement, spacing and how words stack, which is most of the
reason to sweep at all. Use `tools/sweep.sh` to sign off on how it looks.

**It is not a model of the watchface.** A second implementation of the layout
would agree on the day it was written and drift silently afterwards, and
every disagreement would be ambiguous — is the watchface wrong, or the model?
So `tools/test/dump_layout.c` `#include`s `src/c/handwritten.c` and calls the
shipping `refresh()` and `build_face()`, exactly as the test harness does,
then prints where every word landed. This script only pastes the real
bitmaps at those coordinates. There is no layout in Python to drift, and the
dumper is rebuilt on every run so it cannot go stale.

The time and date lists are read **out of `date_sweep.sh`**, not copied, so
the two sweeps cannot quietly stop covering the same ground.

| | |
|---|---|
| faithful | placement to the pixel; which words each setting chooses; the real bitmaps |
| approximated | colour — the palette ramp follows `tint()` and `to_palette()`, then rounds to Pebble's two bits per channel, but it is this script's arithmetic and not the SDK's |
| absent | the reveal animation, and whatever the SDK does to a bitmap on the way to the panel |

### `emu-set-time` does not redraw the watchface

This is the single most important thing to know about sweeping, and it cost
three full sweeps to find.

`emu-set-time` moves the emulator's clock and does nothing else. The face is
rebuilt by `tick_handler`, which the firmware calls when its **own** clock
crosses a minute boundary — and a clock that has been shoved sideways has not
crossed anything. So the face only ever changed once a real minute, showing
whatever the RTC happened to hold at that instant.

At roughly six seconds a shot, that is one picture for every nine files.
151 shots were taken and believed; they contained **19 distinct images**, and
not one was guaranteed to be the time named on it. The duplicates were the
obvious damage. The shots that *did* differ were no more trustworthy.

**The fix is to stop fighting the tick and use it.** To show 09:15, set the
clock to 09:14:57 — `LEAD` seconds early — and wait. The emulator's clock
runs on, crosses into 09:15:00 by itself, fires the tick, and the face is
rebuilt at exactly the wanted time. No relaunch, no buttons, no change to the
watchface, and the normal incremental reveal is exercised rather than a
forced full redraw.

#### And any `pebble` command resets the clock to real time

Found by leaving the clock alone for 70 seconds — it held, and ticked on to
the next minute correctly — then taking two screenshots a minute apart. The
first showed the time that had been set; the second had snapped back to the
real date and time. The emulator runs **pypkjs** alongside QEMU as a stand-in
phone, and a connecting client re-syncs the watch to the host clock.

So between setting the clock and taking the shot, **exactly one command may
run, and it has to be the screenshot.** No polling, no second frame, no
waiting for the reveal to settle and no checking afterwards whether it had —
both of those need a second screenshot, and the second screenshot is what
destroys the thing being photographed.

An intermediate version of this script polled for a still frame, which is
exactly why it produced a sweep of real-time screenshots: every extra frame
shoved the clock back, and "two identical frames" then settled quite happily
on the real time.

#### So the sweep needs its own build

One screenshot is only safe if the face is already complete when it is taken,
so sweeps run with **`WRITE_SPEED 0`** in `config.h` — the face drawn whole,
no reveal — which has to go back before shipping, because the reveal is the
watchface.

**Use `tools/sweep.sh` rather than doing that by hand.** It sets the value,
syncs, builds, installs, sweeps, then puts it back and rebuilds. The restore
is a trap on EXIT, so Ctrl-C, a failed build or a failed sweep all still
leave the source shippable; if the *rebuild* fails it says so, because at
that point the emulator is still holding the sweep build and nothing in the
tree records it. `tools/sweep.sh --restore` fixes up an interrupted run.

Two states of this tree disagreeing is the shape of every serious bug here so
far — geometry from one machine and images from another, font images checked
out from git after `tune.py` wrote them. A sweep build left in place would be
the same mistake and a quiet one: the watchface would simply stop animating,
and look like it had always been that way.

Safe to sweep with: it changes only how the final frame is *reached*, not
what it holds, and the final frame is the entire point of a sweep. Verified —
`run.sh` returns byte-identical results either way (same `573px` longest
reveal, same 6,985,398 checks), because the harness plays the reveal at the
shipping speed regardless and prints a `SWEEP BUILD` banner when `config.h`
says zero.

`date_sweep.sh` warns if the source tree has a non-zero `WRITE_SPEED`. It
can only read the source, not what is installed on the emulator, so it is a
warning rather than a refusal.

| variable | default | is |
|---|---|---|
| `LEAD` | `3` | whole seconds to set the clock early by. Must exceed how long `emu-set-time` takes to land — too short and the clock is already past the boundary on arrival, the rollover never happens, and the shot duplicates its predecessor |
| `WAIT` | `4.5` | seconds from setting the clock to taking the shot |
| `RETRIES` | `3` | attempts at a shot that came out unchanged |

#### Three dead ends, recorded so they are not tried again

- **Waiting longer.** A fixed 3.5s. The face is not slow, it is not being
  redrawn at all.
- **Polling until the picture stops moving.** Worse than useless: it is the
  polling that resets the clock.
- **`emu-button click select` / `back`,** to leave the watchface and return —
  the manual trick that appears to unstick it. Four of five test shots came
  back byte-identical. The window is re-shown and the layer redrawn, but
  `update_proc` draws `s_face`, which was built at the last tick and is
  stale. Only `window_load` rebuilds it, and that runs on app launch.

#### Each shot is compared to the one before it

Its absence is why three sweeps were believed. The check costs nothing — an
md5 of a file already on disk, with no connection to the emulator — so it
always runs. An identical shot re-arms the clock and tries again, up to
`RETRIES`, and says `UNCHANGED` if it still will not move. Not proof of a bug
— in the rounded modes two different times genuinely draw one face — so it
reports rather than fails. What it rules out is finding this by eye, three
sweeps later.
The first shot has nothing before it, so the comparison is primed from the
screen as it is before the clock is touched.

### Timezones

The watchface calls `localtime()`, which is correct and not up for
negotiation — a watch shows the time where its wearer is. The emulator
therefore displays **local** time, and the sweep has to speak local time
back to it.

`pebble emu-set-time` takes *"Unix UTC seconds"* and ignores `--utc` for
that form, so an epoch built as UTC arrives on screen shifted by the
emulator's offset. That was a real bug in the first version of this script:
it asked for 00:00 and the watch displayed 07:00, putting every shot in the
time sweep seven hours off the name on its own file.

`TZ_OFFSET` is the offset the **emulator** displays, in hours east of UTC.
It defaults to this machine's, which is right when the emulator inherits the
host zone — an assumption, and not a documented one, so the script prints
what it assumed:

```
emulator emery, 3.5s settle, assuming it displays UTC+7
```

**Check one shot against its own filename.** If the time is wrong, set the
offset and re-run:

```
TZ_OFFSET=7 tools/date_sweep.sh
```

The date sweep still uses midday, now for a smaller reason: if `TZ_OFFSET`
is wrong the times will be off, but the **date** on screen holds for any
error up to 11 hours. A month sweep landing on the wrong month is a worse
failure than one landing on the wrong hour.

## 11b. Generated files and `git checkout`

**Never restore `resources/images/`, `src/c/geometry.h` or
`tools/test/generated.h` from git on their own.** They are four outputs of
one run of `tune.py` and are only meaningful together — restoring some of
them leaves `geometry.h` describing images that are not there any more, and
the watchface then draws each bitmap into a rect that is not its size. A rect
larger than the image **repeats** it, so the result is a smeared or doubled
letter, not an error.

If a run of `tune.py` produced output you do not want, the fix is to run it
again — not to check the previous output back in. `tools/check_resources.py`
catches the mismatch either way, and both `run.sh` and `date_sweep.sh` refuse
to proceed when it fires.

## 11c. When a family box changes size: `tune.py --rebox`

`extract_handwriting.py` cuts every word to the family box it reads out of
`geometry.h`. `tune.py` writes `geometry.h`, and refuses to write it while
the images on disk disagree with it. So the moment a family box changes
size, **neither tool can go first**: the ink is stale until `geometry.h`
moves, and `geometry.h` will not move until the ink is re-cut.

Two ordinary things change the box. Adding a word can widen a family — the
weekdays did. And the box is measured from *real ink*, so a machine whose
FreeType rasterises one descender a row shorter measures a box one pixel
shorter; **the generated files only describe the machine that made them.**
That second case is what anyone picking the project up on a new machine
walks straight into.

Three steps, in this order:

```
python3 tools/tune.py --rebox          # new geometry; the mis-cut words
                                       # fall back to the font for now
python3 tools/extract_handwriting.py   # re-cut them to the new box
python3 tools/tune.py                  # embed them again — no flag
```

Nothing is lost. The ink lives in `handwriting-templates/drawn/` and is
re-cut from the PDFs every time; a word on the font in between is legible and
correct, just not yet handwritten. `--rebox` names every word it sets aside,
so it cannot half-drop the handwriting quietly — which is the failure this
whole guard exists to prevent.

The flag is only ever needed for the *first* of those runs. If the third one
still refuses, the box moved again, which means something other than the box
size is wrong.

## 11d. The whole loop, in order

From new ink on a drawn page to screenshots to the watch. Two folders are
involved and it matters which one you are in: everything is **authored and
generated in the git folder**, and the build folder only ever receives a
copy (§ `tools/sync.sh`).

**Do not paste this as one block.** Steps 2 and 5 are meant to be read
before continuing, and `run.sh` currently exits non-zero — see below.

```bash
# 1. Regenerate, in the git folder
cd /mnt/c/Users/chron/Documents/GitHub/handwriting-as-spoken-watchface
python3 tools/extract_handwriting.py    # re-cut the ink from the PDFs
python3 tools/tune.py                   # geometry.h, generated.h, images

# 2. Check the numbers describe the files
python3 tools/check_resources.py        # must say "N word images match"
tools/test/run.sh                       # layout suite

# 3. Copy to the build folder and build
bash tools/sync.sh
cd /home/chrono/handwritten-british
pebble build

# 4. Emulator
pebble install --emulator emery

# 5. Screenshot sweep, back in the git folder. This handles the sweep build
#    itself: WRITE_SPEED 0, sync, build, install, sweep, then put it all
#    back. Do NOT call tools/date_sweep.sh directly.
cd /mnt/c/Users/chron/Documents/GitHub/handwriting-as-spoken-watchface
tools/sweep.sh                          # days, dates, times -> shots/

# 6. The real watch, from the build folder
cd /home/chrono/handwritten-british
pebble install --cloudpebble
```

**If step 1 refuses** with words "cut against a different box", the family
box has moved — run §11c's three-step `--rebox` sequence in place of step 1,
then carry on from step 2.

**`run.sh` currently fails**, on the handwriting interweave check, pending
the layout-versus-limit decision in `CONTEXT.md` §8. That is a known state
and not a reason to stop, but it does mean `run.sh` must not be chained with
`&&` — everything after it would be skipped.

**Resources are about 132 kB** against a 256 kB publishing limit, since a
build carries one typeface. §7b.

**Step 5 needs a sweep build**, because only one screenshot can be taken per
shot (§11) so the face has to arrive complete. `tools/sweep.sh` does the
whole dance — `WRITE_SPEED 0`, sync, build, install, sweep, then back to the
shipping value and rebuild — and restores the source from a trap, so an
interrupted or failed run still leaves the tree shippable. It takes the same
arguments as `date_sweep.sh`.

Step 5 also needs the emulator from step 4 still running, and it re-runs
`check_resources.py` first — it refuses to photograph a tree whose images
and geometry disagree, because a sweep of smeared digits that is then
trusted is worse than no sweep.

Step 6 needs `pebble login` and Dev Connect on the phone, both one-off; §7.
It installs the same build as step 4, so there is no need to rebuild between
them.

**Then commit** what `tune.py` wrote — `src/c/geometry.h`,
`tools/test/generated.h`, `package.json` and all of `resources/images/`.
They only describe the machine that made them (§11c), so they have to be
committed from the machine that runs this loop.

## 12. What is still outstanding

The full list, with reasoning, is `CONTEXT.md` §8. In short:

- **Bake the offsets** currently sitting in the Clay sliders into
  `config.h` — add the slider value to the constant, then zero the slider.
  No regeneration needed; do run `tools/test/run.sh`.
- ~~Trim the build for publishing.~~ **Done** — the font no longer ships,
  which took resources from 263 kB to about 132 kB against the 256 kB limit.
  Its images and the TTF stay in the repo; see §7b.
- **Look at the dates**, per §11 above.
