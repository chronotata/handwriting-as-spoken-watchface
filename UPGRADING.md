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

## 8. Tuning without a rebuild

Open the watchface's settings in the Pebble phone app. Twelve sliders — one
per row — nudge that row up (negative) or down (positive) by up to 15px,
and apply the moment you change them. No rebuild, no reinstall. The same
page carries the colours, the date on/off toggle, the date format, whether
`minute(s)` is spoken, and the stroke weight.

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
| "just gone" / "nearly", on the hour | the same word above the o'clock wording |
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

## 11. What's next: version 2

Draw over the templates in `handwriting-templates/` — each is already
sized exactly for its slot in the layout. Swapping them in is a resources
change, not a code change; see `CONTEXT.md` §7 for the constraints (the
same 4-level colour ceiling that shaped the current font rendering applies
to scanned artwork too).
