# Handwritten (British) — Project Context

Status: **v1.1 built, installed, and confirmed working on both the emulator
and real Pebble Time 2 hardware.** This document describes what shipped and
why. For the blow-by-blow of how it got here — three failed masking schemes,
the font-vs-bitmap decision, the split-line layout exploration — see the
project's chat history; it isn't repeated here.

---

## 1. What this is

A British-English rewrite of the 2014 `Handwritten` Pebble watchface,
native to the Pebble Time 2 (Emery, 200×228):

1. Speaks the time as it's said — *"quarter past three"*, *"twenty-nine
   minutes to midnight"* — not read off the digits.
2. Adds a date line.
3. Targets Emery only for now; other platforms deferred.

---

## 2. Architecture: pre-rendered word bitmaps

Words are **images, not font text.** `tools/tune.py` renders every word the
face can ever show — 62 of them — to individual PNGs at build-configuration
time; `handwritten.c` only ever blits rectangles of those images.

**Why not a font:** three consecutive attempts to mask *unrevealed* text
during the reveal animation all leaked pixels, because they depended on
knowing exactly where Pebble's font rasteriser placed each glyph — which
could only ever be approximated from the PC side. Switching to bitmaps
removed the need to know that at all: the reveal draws a **sub-rectangle**
of an image (`gbitmap_create_as_sub_bitmap`) rather than drawing everything
and then hiding part of it. Nothing unrevealed is ever drawn, so nothing
can leak.

**Canvas-driven layout.** Each PNG is cropped tight to its own ink — no
shared canvas height across a word family. The C stacks these canvases
directly, so the padding baked into a word's PNG *is* its spacing. Every
visual gap between stacked rows is therefore identical (`ROW_GAP`, 6px),
and can be opened for one specific row later by adding blank rows to that
word's image and re-running `tune.py` — no code change. This was chosen
deliberately over a denser (43px) alternative that would have produced
uneven, non-tunable gaps; 40px was accepted as the trade-off for a real
tuning lever, which also matters directly for v2 hand-drawn artwork.

**Colour: 4-bit palettised, decoded not indexed.** Images carry 16 shades
so edges can be antialiased. The source palette colours are arbitrary —
`tune.py` encodes each intensity level into the red/green channels rather
than as greyscale, and `handwritten.c` *decodes* the level from whatever
colour lands in each palette slot at runtime, rather than trusting its
index position. Both choices exist because of two real bugs (§6) where a
greyscale palette collapsed during SDK conversion, and where index order
didn't survive it either.

**On-demand loading.** Bitmaps load lazily and are pruned to whatever's
currently on screen (`prune_cache()`), avoiding the 2014 original's
per-word-layer memory leak.

---

## 3. Layout model

### 3.1 Rows

| Row | Content | Indent |
|---|---|---|
| `ROW_MINUTE` | minute number, or first half of a split word | 0 |
| middle (row/tag varies) | split word's second half, or `minutes` on its own line | 1 (when present) |
| `ROW_RELATION` | `past` / `to` | **2, always** |
| `ROW_HOUR` | the hour word | **3, always** |
| `ROW_SOLO` | `midnight` / `midday` alone | centred |
| `ROW_DATE` | the date line | centred, own baseline |

**Relation and hour are pinned to indent 2/3 regardless of what's above
them.** This was a deliberate late fix: indenting by *array position*
meant `past`/`one` sat at different x depending on whether a `minutes` row
existed above them, causing pointless redraws crossing e.g. `:14`→`:15`
(*fourteen minutes past one* → *quarter past one*). Fixed indents by role
mean neither ever redraws unless the word itself changes.

### 3.2 Vertical anchoring

The relation row's canvas top is pinned at `REL_TOP` (100); the hour row
stacks directly below it. Both hold position for the whole half-hour
(`:01`–`:30` and `:31`–`:59`) and are **never redrawn** by a minute tick
that only changes the row above them. Only the floating row(s) above
`REL_TOP` move, into space reserved for the worst case: `twenty-` / `eight`
/ `past` / `midnight` at `:28`.

### 3.3 Compound and split words

Minute words 21–29 (except 25, which behaves like any other multiple of 5)
split across two lines — `twenty-` / `seven` — freeing width versus one
long word. `minutes`/`minute` rides inline beside the short second half
when split, or takes its own row when the minute word isn't split. Either
way it never moves independently — it's positioned relative to its host
word.

### 3.4 The date

Built from atoms (digit images, month images, ordinal images), not a
formatted string — no printf-style date code exists. Always centred, on a
**fixed baseline** (`DATE_BASELINE`, 211) independent of which month or day
is showing, so months without descenders don't sit lower than months with
them. The ordinal (`st`/`nd`/`rd`/`th`) is raised `ORD_RISE` (6px) above
that baseline by its own baseline, not by aligning its canvas top to the
digit's — digits have no descenders, so top-alignment barely raised it at
all (§6 has the numbers).

### 3.5 Wording rules

```
03:00 exactly     -> "the" "witching" "hour"      (easter egg, full minute)
m==0,  h==0       -> "midnight"                    (solo)
m==0,  h==12      -> "midday"                       (solo)
m==0               -> <hour> "o' clock"             (note: half-space, not full)
m==15              -> "quarter" "past" <hour>
m==30              -> "half"    "past" <hour>
m==45              -> "quarter" "to"   <hour+1>
m<30, not above    -> <mins>    "past" <hour>
m>30, not above    -> <60-mins> "to"   <hour+1>
```

Twelve o'clock is asymmetric on purpose: `midday` only when it stands
alone, `noon` after it (*past*), `twelve` before it (*to*); `midnight` on
both sides.

---

## 4. Tunable constants (`config.h`)

| | Value | |
|---|---|---|
| `FONT_SIZE_TIME` | 40 | minute number, relation, hour |
| `FONT_SIZE_SOLO` | 46 | solo midnight/midday |
| `FONT_SIZE_MINS` | 25 | minute(s) annotation |
| `FONT_SIZE_DATE` | 24 | day digits, year, month |
| `FONT_SIZE_ORD` | 15 | st/nd/rd/th |
| `INDENT` | 9 | staircase step |
| `ROW_GAP` | 6 | uniform ink-to-ink gap between stacked rows |
| `REL_TOP` | 100 | pinned canvas top of the relation row |
| `DATE_BASELINE` | 211 | fixed regardless of month/day |
| `ORD_RISE` | 6 | ordinal's baseline rise above the date baseline |
| `DATE_SPACE` | 7 | gap before the month, and before the year |
| `MIN_TRAIL` | 6 | horizontal gap before an inline `minutes` |
| `OFFSET_*` | 0 each | live per-row nudge, exposed as Clay sliders, ±15px |

**Changing a `FONT_SIZE_*` requires re-running `tools/tune.py`** — it
re-renders every affected image and regenerates `geometry.h`. Changing any
other constant only needs a rebuild.

---

## 5. Verification method

Every layout claim in this document has been checked by compiling
`handwritten.c` against a hand-written Pebble API stub and running its
*real* `build_face()` over all 1440 minutes of the day (a small C test
harness, not a Python re-implementation) — checking bounds, indent
consistency, animation order, and stacked-row gap uniformity directly
against the shipping code. This caught real bugs a python model would not
have (the `kOnes[19]` out-of-bounds read at `:20`/`:40` being the clearest
example) and is the standard to hold any future change to.

---

## 6. Bugs found after shipping, and their fixes

Kept because each one is a real lesson about the platform, not just a
changelog entry.

**Reveal fragments (masking era, pre-bitmap).** Three consecutive attempts
to size a mask from font metrics all leaked pixels — first from `FreeType`
vs Pebble's own rasteriser disagreeing, then from tall ascenders/descenders
exceeding the reported text box, then from per-size metric mismatches.
Resolved only by removing masking entirely (§2).

**Blank screen on first bitmap build.** The generated PNGs used a
16-level *greyscale* palette. Pebble has 2 bits per channel — only four
greys exist — so the SDK's palette conversion collapsed 16 levels to 4
colours and re-indexed every pixel to match, destroying the
index-to-intensity mapping the runtime tint depended on. Fixed by encoding
each level as a *distinct* colour (not greyscale) in the source PNG; the
runtime palette rewrite makes the source colours irrelevant beyond that.

**Grey text, not white.** Two compounding causes in `tint()`: it assumed
palette index order survived SDK conversion (it doesn't — always decode
from the colour actually present, never trust position), and it blended
colours in 2-bit space, truncating so hard that only the single brightest
level ever reached full white. Fixed by decoding intensity from the
colour itself and blending at full 8-bit precision before rounding down
to Pebble's four levels.

**Still grey after that.** Not a bug — Pebble's four-levels-per-channel
limit means a desktop's dozens of antialiasing shades collapse to two or
three chunky greys, and on a one-word-per-canvas image the halo dominates
perception even when the stroke core is genuinely white. Fixed with a
contrast stretch in `tune.py` (`INK_LO`/`INK_HI`, 70/150) that pushes
faint coverage to background and strong coverage to full ink, keeping
only a 1px soft edge. This is the crisp-vs-soft tuning knob for both v1
and, later, scanned handwriting in v2.

**`:20`/`:40` showing "one" instead of "twenty".** `kOnes[]` holds 19
entries (*one*..*nineteen*); `mins == 20` read `kOnes[19]`, one past the
end. Undefined behaviour, not a crash — which is exactly why it silently
drew the wrong word rather than failing loudly. Fixed with an explicit
`mins == 20` case.

**Unnecessary redraws across row-count changes.** Covered in §3.1 — fixed
indents by role rather than array position.

**Ordinal sitting too low.** Digits have no descenders, so their canvas
height already equals their distance to baseline — "align tops" (the
original approach) only raised the ordinal 1–3px, and inconsistently
between suffixes since `st` and `nd` aren't the same height. Fixed by
raising the ordinal's own *baseline* by a fixed amount (`ORD_RISE`)
instead of aligning canvas tops.

**Reveal animating out of natural reading order.** The split-word
`minutes` element was appended to the layout array *after* the relation
and hour words, because its position depends on its neighbour and was
easiest to compute last — but array order is also animation order. Fixed
with an `insert_element()` helper that places it at the correct array
position (right after the word it follows) while leaving its computed
geometry untouched. A reminder that *when something is computed* and
*where it belongs in the reveal sequence* are unrelated, and conflating
them was the actual bug.

**Settings page invisible on the phone, despite Clay being correctly
wired up end to end.** Two separate causes, both in `package.json`, both
silent — see `UPGRADING.md` §8 for the detail and how to avoid repeating
this in any project, not just this one:

- `pebble.capabilities` never contained `"configurable"`. Without it the
  phone app has no way to know a settings page exists at all — Clay being
  perfectly implemented on both sides is irrelevant if the manifest
  doesn't advertise it.
- Separately, `dependencies` kept silently losing its `pebble-clay` entry
  every time `package.json` was regenerated wholesale rather than edited
  in place. This one is more insidious: the build kept succeeding anyway,
  because `node_modules/pebble-clay` was still on disk from the original
  `pebble package install`, masking the missing manifest entry until a
  clean environment would have exposed it.

---

## 7. Version 2 — the user's own handwriting

Not started. The architecture is already shaped for it:

- `tools/tune.py` exports `handwriting-templates/` — all 62 words as plain
  PNGs at their exact final canvas sizes, ready to draw over.
- Swapping in real handwriting is a resource-and-`geometry.h` change; no
  layout, anchoring, or animation code should need to move.
- The same four-level colour ceiling applies to scanned artwork as to the
  rendered font — bold, high-contrast strokes will reproduce far better
  than soft pencil gradations.
- User has a further idea for the drawing/scanning pipeline itself, not
  yet discussed.

---

## 8. Open items

None blocking. Possible future directions, none committed: extending
`OFFSET_*` tuning to include per-row size multipliers (currently sizes are
build-time only, per §4); other Pebble platforms; v2 artwork.
