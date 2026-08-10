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

This renders all 69 words the face can show into `resources/images/`,
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

It also writes `handwriting-templates/` — the same 69 words as plain PNGs
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
once per date format and minutes mode — about 684,000 assertions, under
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
pebble emu-set-time --emulator emery 00:28   # worst-case 4-row split phrase
pebble emu-set-time --emulator emery 23:40   # regression check: must show "twenty", not "one"
pebble emu-set-time --emulator emery 03:00   # the witching-hour easter egg
pebble emu-set-time --emulator emery 12:00   # solo "midday"
```
`pebble logs --emulator emery` shows live log output if anything looks off.

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

Open the watchface's settings in the Pebble phone app. Seven sliders — one
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

It changes how solidly the outline is inked, **not how far it extends** —
the outer edge of the outline sets the apparent width either way, so Solid
reads harder-edged rather than thicker. To make the strokes genuinely
thicker, raise `BOLD_BLEND` in `tools/tune.py` and regenerate.

Three of those sliders cover the rows above *past*/*to*, and they are
separate on purpose: **Split number, top half** moves only `twenty-`, so it
can be pulled clear of the screen edge on its own; **Minute number** moves
the number itself, including the second half of a split word; and
**"minute(s)"** places the annotation. Keep the last two at the same value
and the annotation stays level with the number — differ them only if you
want it deliberately off the line.

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
