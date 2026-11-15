# CLAUDE.md — working on Handwritten As Spoken

A Pebble Time 2 (emery, 200×228) watchface that tells the time the way it is
said — *"quarter past three"* — with every word drawn by hand. **Published**
on the Pebble appstore as 1.3.1:
https://apps.repebble.com/e6bd4067de2549e2bdc4d10f

## Read these first

- **`CONTEXT.md`** — architecture, every design decision and why, the bugs
  already found, and **§8, the open-items list**. Start there for any change.
- **`UPGRADING.md`** — environment setup, and **§11d, the whole loop** from
  new ink to the watch. §11 covers screenshots, §7b the typefaces and icons.
- `README.md` — the public face of the project.

The code comments are long on purpose: most explain a bug that shaped the
line. Read them before "simplifying" anything.

## Ground rules

**1. Generated files come from the maintainer's machine only.**
`tools/tune.py` writes `src/c/geometry.h`, `tools/test/generated.h`,
`resources/images/w_*.png` and the media list in `package.json`. It
rasterises a TTF, and FreeType differs by a pixel between machines (the DATE
box measures 28px on the maintainer's, 29px elsewhere). Do **not** run
`tune.py` and leave its output in the tree from any other environment. Edit
`tune.py` or `config.h`, then ask the maintainer to run it. To verify a change
yourself, copy the repo somewhere temporary and run it there.
`tools/extract_handwriting.py` and `tools/make_icons.py` touch no font and are
safe to run anywhere.

**2. Never hand-edit or regenerate `package.json` wholesale.** `tune.py`
rewrites the whole `resources.media` list on every run, so hand-added media
vanish; add them in `tune.py` (see how `menu_icon.png` is done). Regenerating
the file from scratch also drops the `pebble-clay` dependency. Edit keys in
place.

**3. Verification standard.** `tools/test/run.sh` compiles the real
`src/c/handwritten.c` against a Pebble API stub and sweeps every minute of
the day in every setting. It must report **0 failures**. When adding a check,
prove it can fail by injecting the bug it targets. Never verify layout with a
re-implementation in another language — it agrees with itself, not the watch.

Faster tools, all built on the same real layout code:

- `tools/mock_sweep.py all --families` — screenshots without an emulator, in
  about two seconds. Use it for placement questions.
- `tools/preview_offset.py NAME=value ...` — before changing any `config.h`
  constant, shows every face it would touch, with before/after pictures.
- `tools/inspect_failures.py` — one picture per distinct collision if
  `run.sh` ever fails.

**4. Two folders, one direction.** Everything is authored and generated in
the git folder. A separate build folder on the Linux filesystem (faster for
`pebble build`) receives a one-way copy via `tools/sync.sh`. That script is
**gitignored** because it holds machine paths, so a fresh clone needs its own.
Never edit in the build folder; the next sync overwrites it and deletes
anything it doesn't know about.

**5. Offsets.** `OFFSET_*` in `config.h` are the tuned design; the Clay
sliders on the phone are deviations added on top, all zero by default. An
offset change needs no regeneration. `OFFSET_RELATION` moves "past" and "to"
together; `OFFSET_PAST` is the difference between them.

**6. The settings wire format is append-only.** Message keys in
`package.json` and fields in the persisted `Settings` struct are positional:
add new ones at the end, never reorder or delete. `typeface_unused` and the
`Typeface` key are reserved slots, kept so existing users' saved offsets
don't shift.

**7. One typeface per build.** `python3 tools/tune.py` builds the handwriting
(the default); `--font` builds the bundled font instead. Without `--font`
every word must be drawn — `tune.py` refuses otherwise.

**8. The emulator lies about time.** `pebble emu-set-time` does not redraw
the face (it only repaints on its own minute rollover), and **any** `pebble`
command — screenshots included — resets the emulator's clock to real time.
`tools/sweep.sh` handles this; read UPGRADING §11 before improvising. The
emulator also has no settings page: to test a setting there, change its
`DEFAULT_*` in `config.h` temporarily, and put it back.

## Releasing

- Version lives in `package.json` (and `package-lock.json`). The appstore
  accepts patch bumps like 1.3.1 despite Pebble's docs saying `x.y.0`. It is
  **not confirmed** that existing installs get offered a patch update — the
  watch binary only stores major.minor — so use a minor bump for anything
  users must receive.
- `store/` holds the listing: `description.txt`, the screenshots and
  `write-on.gif`, and the store icons. `tools/make_icons.py` regenerates the
  icons, including the 25×25 watch launcher icon in `resources/images/`.
- Publishing goes through the Developer Dashboard at
  developer.repebble.com: **New Release**, upload the `.pbw` from the build
  folder. Releases go live immediately; there is no review.

## Working with the maintainer

- They build in **WSL2 / Ubuntu**. Write `python3`, never bare `python`.
- Install to the real watch with **`pebble install --cloudpebble`**, not
  `--phone`.
- They run commands and commit themselves. Give one step at a time, with
  what to expect, and say plainly what you have and haven't verified.
- Check facts before stating them — what the store accepts, what the SDK
  does, what a file contains — rather than reasoning from memory.

## Status

Nothing is broken and nothing is blocking. Ideas and watch-items are in
`CONTEXT.md` §8.
