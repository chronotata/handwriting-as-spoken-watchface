# Handwritten (British) — Project Context

Status: **built, installed, and confirmed working on both the emulator and
real Pebble Time 2 hardware**, including the uniform family boxes (§2, §3.2)
and the selectable date formats (§3.4) — both of which were arithmetic and a
layout sweep only until they were seen on the watch. This
document describes what shipped and why. For the blow-by-blow of how it got here — three failed masking schemes,
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
face can ever show — 69 of them — to individual PNGs at build-configuration
time; `handwritten.c` only ever blits rectangles of those images.

**Why not a font:** three consecutive attempts to mask *unrevealed* text
during the reveal animation all leaked pixels, because they depended on
knowing exactly where Pebble's font rasteriser placed each glyph — which
could only ever be approximated from the PC side. Switching to bitmaps
removed the need to know that at all: the reveal draws a **sub-rectangle**
of an image (`gbitmap_create_as_sub_bitmap`) rather than drawing everything
and then hiding part of it. Nothing unrevealed is ever drawn, so nothing
can leak.

**Canvas-driven layout, uniform per family.** Every PNG in a size family
shares one canvas *height* — the union of that family's ink extents,
ascender line to descender line — with the baseline at the same offset in
all of them. Widths stay tight to each word's own ink. `tune.py` measures
the boxes and emits them as `TIME_BOX_H` / `TIME_BOX_BASE` and so on.

The consequence is a **constant row pitch** (`TIME_BOX_H + ROW_GAP`, 51px).
A word's neighbours cannot move when it is replaced by a different word,
and baselines land on the same lines all day — the stacked layout uses
y = 25 / 76 / 127 / 178, always. See §3.2 for why that matters.

Families are the five *size* families (`TIME`, `SOLO`, `MINS`, `DATE`,
`ORD`), not roles. Role-based families were considered and rejected because
roles overlap: *four* is both a minute word and an hour word, so role
families would need two rendered copies of every shared word. `TIME`
contains every role group, so unifying it unifies all of them for free.

Font metrics are deliberately not used to size the box. A script face's
declared ascent and descent are far larger than anything it actually draws,
and padding to them would waste around 20px a row this layout cannot spare.

**This reverses the original tight-canvas decision, on purpose.** Tight
canvases made every visual ink gap exactly `ROW_GAP`; that is now gone.
`ROW_GAP` is only the *minimum* gap, seen when a full descender sits
directly above a full ascender (*"eight o' clock"*); elsewhere it opens up
by whatever ink the two words leave unused, to `ROW_GAP + 31` for *to*
above *one*. Even ink gaps were traded for even baselines because ruled
paper is the better model for a handwriting face, and because it is a far
better substrate to draw v2 artwork over — a tight *one* was 16px tall with
nowhere to put a flourish; on the family box it is 47px.

Per-row spacing is still tunable the same way: add blank rows to a word's
PNG and re-run `tune.py`.

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
| `ROW_SOLO` | `midnight` / `midday` alone | 0 (centred *vertically*) |
| `ROW_DATE` | the date line | centred horizontally, own baseline |

Only the date line is centred horizontally. Everything else — including the
solo `midnight`/`midday` — sits at the left margin and steps right by
`INDENT`; "centred" for the solo row means vertically, in the space above
the date.

**Relation and hour are pinned to indent 2/3 regardless of what's above
them.** This was a deliberate late fix: indenting by *array position*
meant `past`/`one` sat at different x depending on whether a `minutes` row
existed above them, causing pointless redraws crossing e.g. `:14`→`:15`
(*fourteen minutes past one* → *quarter past one*). Fixed indents by role
mean neither ever redraws unless the word itself changes.

### 3.2 Vertical anchoring

The relation row's canvas top is pinned at `REL_TOP` (96); the hour row
stacks directly below it. Both hold position for the whole half-hour
(`:01`–`:30` and `:31`–`:59`) and are **never redrawn** by a minute tick
that only changes the row above them. Only the floating row(s) above
`REL_TOP` move, into space reserved for the worst case: `twenty-` / `eight`
/ `past` / `midnight` at `:28`.

Because the row pitch is now constant (§2), the rows above `REL_TOP` no
longer move either, as long as the row *count* is unchanged. Previously
`twenty-` shifted by up to 12px whenever the second half of a split word
changed height — *twenty-three* → *twenty-four* moved it 12px and
re-animated it — because rows were stacked upward by measured height.
Sweeping all 1440 minutes, that alone was **336 redraws a day**, and it is
now zero:

| redraws per day | tight | uniform |
|---|---|---|
| genuinely a new word (unavoidable) | 1819 | 1819 |
| same word, position only | 747 | **411** |

The 411 that remain are dominated by `minutes` (288), which moves
*horizontally*: in the split layout its x is `host.x + host.width +
MIN_TRAIL`, chasing the width of *one*..*nine* (41–77px). Uniform heights
cannot fix that; pinning it to a fixed column would, at the cost of a
visible gap after short words. Left as-is deliberately.

**The vertical budget is tight and is checked, not assumed.** The worst case
is four `TIME` rows: 4 × 47 + 3 × `ROW_GAP`. At the shipped constants the
phrase ink spans y = 1..194 with the date's ascenders starting at 196 — 2px
of clearance. `tools/tune.py` recomputes this from the real ink extents on
every run and refuses to generate if it fails, naming the constant to
change; `handwritten.c` carries `_Static_assert`s as a backstop for the case
where `config.h` is edited without re-running `tune.py`. Raising
`FONT_SIZE_TIME` or `ROW_GAP` is what will trip them.

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

**Formats are data, not code.** An *atom* is one readable unit — the
weekday, the day-with-its-ordinal, the month, the year — which may expand to
several word images ("21st" is three). A format is an ordered list of atoms
in `kDateFormats[]`, and `DATE_SPACE` goes **between** atoms: never before
the first, never after the last, never inside one. Adding a format means
adding a row to that table, a matching option in `src/pkjs/config.js`, and
nothing else — `build_date()` should not need to change.

```
0   "10th Aug. 2026"    DAY MONTH YEAR
1   "Mon. 10th Aug."    WEEKDAY DAY MONTH
```

The index is the wire format — it is what the phone stores and sends — so
**new formats go on the end** and existing rows are never reordered or
renumbered. Doing either would silently change the date for everyone who
had already chosen one.

That positional spacing rule replaced an identity-based one ("add a space
after the ordinal and after the month"). The two agree exactly for format 0,
which is why the switch was safe, but identity-based spacing only worked
because the month happened to be followed by the year; in any format ending
on the month it would have appended a phantom trailing gap and thrown the
centring off by 3px. The harness pins this down both ways — see §5.

`kWeekdays[]` is indexed by `struct tm`'s `tm_wday`, so it is **Sunday
first**, as is `WEEKDAYS` in `tune.py`. Rotating either one shifts every
weekday by a day without failing to build.

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
| `ROW_GAP` | 4 | canvas-to-canvas gap; the *minimum* visible ink gap |
| `REL_TOP` | 96 | pinned canvas top of the relation row |
| `DATE_BASELINE` | 215 | fixed regardless of month/day |
| `ORD_RISE` | 6 | ordinal's baseline rise above the date baseline |
| `DATE_SPACE` | 7 | gap before the month, and before the year |
| `MIN_TRAIL` | 6 | horizontal gap before an inline `minutes` |
| `OFFSET_*` | 0 each | live per-row nudge, exposed as Clay sliders, ±15px |

**Changing a `FONT_SIZE_*` requires re-running `tools/tune.py`** — it
re-renders every affected image and regenerates `geometry.h`. Changing any
other constant only needs a rebuild.

`DATE_TOP_LIMIT` (194) is not independent: keep it 2px above
`DATE_BASELINE - DATE_BOX_BASE`. A `_Static_assert` enforces it.

---

## 5. Verification method

```
python3 tools/tune.py    # writes tools/test/generated.h
tools/test/run.sh        # needs gcc; no Pebble SDK
```

Every layout claim in this document is checked by `tools/test/`, which
compiles the *real* `src/c/handwritten.c` against a hand-written Pebble API
stub (`pebble.h`, `stub.c`) and sweeps its own `build_face()` over all 1440
minutes of the day, every date of a leap year, and the asymmetric wording
cases — **once per date format**, driven off `DATE_FORMAT_COUNT` so a new
format is swept without anyone remembering to widen a loop. Around 250,000
assertions, under a second.

It is deliberately not a Python re-implementation. A model agrees with
itself, not with the watch: the `kOnes[19]` out-of-bounds read at
`:20`/`:40` was undefined behaviour that drew a plausible wrong word rather
than crashing, and a model would have had its own array and its own bug, or
neither. Here `-fsanitize=address,undefined` reports it outright as
`index 19 out of bounds for type 'uint8_t [19]'`.

What it checks:

| | |
|---|---|
| bounds | every word's **ink** on screen and clear of the date's ascenders — not its canvas, which legitimately overhangs the top (§3.2) |
| indents | relation at 2 steps, hour at 3, by role rather than array position |
| stacking | consecutive canvases exactly `ROW_GAP` apart, inline `minutes` excepted |
| reveal order | array order equals reading order |
| pinned rows | relation at `REL_TOP`, hour one pitch below, and `twenty-` never moving |
| date | one fixed baseline for every month, ordinal raised, line centred |
| date formats | format 0 compared element-for-element against a verbatim transcription of the pre-refactor builder; format 1's weekday checked against an independent `tm_wday`, and asserted to appear first with no year |
| settings | `tuple_int()` reads Clay's cstring **and** int tuples; an out-of-range format index falls back rather than indexing off `kDateFormats` |
| memory | the reveal is played out frame by frame, so `prune_cache()` and the sub-bitmap slicing run for real under ASAN |

The suite is only worth what it catches, so it has been checked against
regressions rather than assumed: re-introducing the `kOnes[19]` read, the
indent-by-position bug, the appended-`minutes` reveal-order bug, an
unpinned relation row, a flattened ordinal, and a `prune_cache()` that
stops freeing all produce failures. So do, since the date formats landed:
an off-by-one `kWeekdays` index, a gap emitted before the first atom
instead of only between atoms, and `make_tm()` leaving `tm_wday` at zero.
Hold future changes to that standard — if a change cannot break a test,
add the test that it would break.

That last one is worth dwelling on. `make_tm()` never set `tm_wday`, and
nothing noticed for as long as nothing read it. The moment a format showed
the weekday, a `memset`-to-zero `tm` would have made every swept date a
Sunday — and the weekday assertion would have passed all year without ever
being tested. A test that cannot fail is worse than no test, because it is
counted. `sweep_dates()` now also asserts that all seven weekdays actually
appeared.

Two things the suite does **not** cover: anything about how the words
actually look (colour, antialiasing, stroke weight — all of §2's palette
work), and the vertical budget arithmetic, which lives in `tune.py` and the
`_Static_assert`s instead. Hardware remains the final check.

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
silent — see `UPGRADING.md` §9 for the detail and how to avoid repeating
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

- `tools/tune.py` exports `handwriting-templates/` — all 69 words as plain
  PNGs at their exact final canvas sizes, ready to draw over. Since the
  family-box change these are uniform per family (47px for time words, 29px
  for date atoms) with the baseline at a fixed offset in every one, so they
  behave like ruled paper and leave real room above and below the baseline.
- Keep the canvas height and the baseline position when redrawing. Widths
  are free — they are per-word and only affect horizontal spacing.
- `minute`/`minutes` are the exception at 15px with the baseline flush to
  the bottom: the rendered font has no descender there, so no room was
  reserved. If hand-drawn versions want a looping *y*, that family's box
  needs padding first (add blank rows to both PNGs and re-run `tune.py`).
- Swapping in real handwriting is a resource-and-`geometry.h` change; no
  layout, anchoring, or animation code should need to move.
- The seven weekday words (`Sun.`–`Sat.`) are templates too, and are only
  drawn by date format 1. They sit in the `DATE` box like the months, so
  they need no special treatment — but they do bring the set to 69.
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

Both date formats are confirmed on hardware. The format machinery is built
for more than two — `UPGRADING.md` §10 is the recipe — but no third format
is planned.

**Two working copies.** The git folder is the only place anything is
authored or generated: edits, `tune.py`, `run.sh`. A separate build folder
on the Linux filesystem receives a one-way copy, because `pebble build` is
far slower over `/mnt/c`. The direction is fixed deliberately.
`package.json` is both hand-authored (`messageKeys`) and generated
(`resources.media`), so when each folder generated its own, each ended up
holding half the truth — one had the weekday bitmaps declared but not the
`DateFormat` key, the other the reverse, and the build failed on a symbol
that was sitting in the other copy. Any file with two authors in two places
will find that bug. The local sync helper is not committed; it has absolute
paths in it.
