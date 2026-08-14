# Handwritten (British) — Project Context

Status: **v1.2 — feature complete for the rendered typeface.** Built,
installed and confirmed working on both the emulator and real Pebble Time 2
hardware, including the uniform family boxes (§2, §3.2) and the selectable
date formats (§3.4) — both of which were arithmetic and a layout sweep only
until they were seen on the watch.

The last round of work — the larger type, the SOLO wording on the hour, and
the three ways of reading the minute — passes the full sweep but is newer
than the last hardware confirmation. Update this line once it has been worn.

This document describes what shipped and why. For the blow-by-blow of how it got here — three failed masking schemes,
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
face can ever show — 83 of them — to individual PNGs at build-configuration
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

The consequence is a **constant row pitch** (`TIME_BOX_H + ROW_GAP`, 50px).
A word's neighbours cannot move when it is replaced by a different word,
and baselines land on the same lines all day — the stacked layout uses
y = 25 / 76 / 127 / 178, always. See §3.2 for why that matters.

Families are the six *size* families (`TIME`, `SOLO`, `MINS`, `DATE`,
`ORD`, `HEDGE`), not roles. `SOLO` covers everything that appears **on the hour** —
the lone `midnight`/`midday`, every *"\<n\> o' clock"*, and the witching
hour — because those phrases are two or three short words with the whole
screen to themselves. That is why `one`..`eleven` are rendered twice, once
as a minute number and once as an hour. Role-based families were considered and rejected because
roles overlap: *four* is both a minute word and an hour word, so role
families would need two rendered copies of every shared word. `TIME`
contains every role group, so unifying it unifies all of them for free.

Font metrics are deliberately not used to size the box. A script face's
declared ascent and descent are far larger than anything it actually draws,
and padding to them would waste around 20px a row this layout cannot spare.

**`ROW_GAP` is negative: the rows interweave on purpose.** A script face
reads better with a descender passing the ascender below it than with every
line held clear, and the space that buys is what let the fonts grow — `TIME`
40 → 44, `MINS` 25 → 27, `SOLO` 46 → 50, measured off a mock-up rather than
guessed.

What it costs is the guarantee `ROW_GAP` used to carry. It is no longer a
floor on the visible ink gap, so `INK_OVERLAP_MAX_PCT` takes over: a
descender may reach at most 25% of the way past the ascender below it, and
no ink may enter the neighbouring line's x-height, where the lowercase
bodies are. Both are swept over every minute rather than eyeballed on the
handful of phrases in a mock-up — the worst pair is the deepest descender
above the tallest ascender, which need not occur in any phrase anyone
happened to look at.

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

**Two weights in one image.** Levels 1–2 of every word carry a slightly
bolder outline of that word; levels 3–15 are the word itself. The palette
decides which you see — the outline renders as paper when the ink is lighter
than the paper, and as ink when it is darker.

This exists because **dark strokes render thinner than light ones on this
panel**. Measured from photographs of the watch, black-on-white came out
**19.7% thinner** than white-on-black (2.22 vs 2.75 device px on *three*,
and the same 0.77–0.83 ratio on three other rows), worst where the strokes
are thinnest. Light strokes bloom; dark ones do not.

Two things it is worth knowing were ruled out first, both by measurement:

- **The bitmaps are not at fault.** Where the ink lands is an exact mirror
  between the two schemes — 82.6% full ink, 7.0% and 7.1% partial, 3.4%
  invisible, identically. In sRGB value terms the difference is zero. It is
  the panel and the eye, not the pipeline.
- **Making every word bolder would not have helped.** It is a *ratio*: that
  lifts both schemes and leaves the gap exactly where it was. Only a
  polarity-dependent lever closes it, and the palette alone is worth at most
  +5.7% — a quarter of what was needed.

Levels 1–2 were free to take: they held the faintest edge of the word, which
already rendered as plain paper in every colour scheme — 3.4% of the ink
pixels doing nothing. Reusing them costs no extra images and no extra
memory, and levels 3–15 keep their old meaning to the pixel, so
**white-on-black is unchanged**. The outline is clipped to the word's
existing canvas rather than its own, so not one word moved: every canvas
size, baseline and family box is exactly as it was.

Worth +15.8% across all 83 words, which targets the *nominal* width baked
into the bitmaps rather than matching white-on-black — that scheme is itself
running about 6% fat from bloom. `DEFAULT_STROKE_WEIGHT` sets how heavily
the outline is inked, and the phone exposes it as a slider; 0 turns it off.

**On-demand loading.** Bitmaps load lazily and are pruned to whatever's
currently on screen (`prune_cache()`), avoiding the 2014 original's
per-word-layer memory leak.

---

## 3. Layout model

### 3.1 Rows

| Row | Content | Indent |
|---|---|---|
| `ROW_HEDGE` | `just gone` / `nearly`, above the stacked phrase | flat, same x as the word below |
| `ROW_HEDGE_SOLO` | the same word above the on-the-hour wording | flat |
| `ROW_SPLIT_HEAD` | first half of a split word — `twenty-` | 0 |
| `ROW_MINUTE` | the number **with** `minute(s)` stacked below it; also the o'clock top word and witching `the` | 0 |
| `ROW_MINUTE_ALONE` | the number with **nothing** below it — `quarter`, `half`, any unannotated number | 0 |
| `ROW_MINUTE_SPLIT` | the lower half of a split word, annotated or not | 1 |
| `ROW_MINUTES_OWN` | `minute(s)` on a line of its own — :01–:20 | 1 |
| `ROW_MINUTES_INLINE` | `minute(s)` riding beside a split number — :21–:29 | hangs off its host |
| `ROW_RELATION` | `past` / `to` | **2, always** |
| `ROW_HOUR` | the hour word | **3, always** |
| `ROW_SOLO` | `midnight` / `midday` alone | 0 (centred *vertically*) |
| `ROW_DATE` | the date line | centred horizontally, own baseline |

Only the date line is centred horizontally. Everything else — including the
solo `midnight`/`midday` — sits at the left margin and steps right by
`INDENT`; "centred" for the solo row means vertically, in the space above
the date.

**Six rows above the relation are nudged separately.** The sliders act per
row, so anything wanting its own placement needs its own row, and the minute
number turned out to want three: with the annotation stacked under it the
gap down to *past*/*to* is filled; with nothing under it the same gap is
empty and reads as floating; and the lower half of a split word sits under
`twenty-`, which balances it differently again.

The pairing that matters: an inline `minute(s)` is pinned to
`ROW_MINUTE_SPLIT`'s baseline, so `OFFSET_MINUTES` should match
`OFFSET_MINUTE_SPLIT` — **not** `OFFSET_MINUTE` — to stay level.

They did not start that way. Both halves of a split word shared
`ROW_MINUTE`, so nudging the head down moved the number with it, while the
inline `minute(s)` — pinned to the number's **baseline** at build time but
carrying its own row's offset at draw time — stayed put. On the watch it
read as the annotation floating; it was the number that had moved. Measured
from a photograph at 4.6px.

Keeping `OffMinutes` equal to `OffMinute` holds the annotation on the
number's line. Differing them is allowed and now means exactly what it says.

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

**How the minute is READ is a setting** (`kRoundingModes`): the exact
minute, the nearest five, or the nearest five with a hedge saying which way
it went — *just gone five past*, *nearly ten past*. Rounding is to the
nearest tick throughout; no landmark gets a wider pull toward the hour or
half. People plainly do reference times against the roundest mark nearby,
but nothing in the literature pins down how wide that window should be, so
the rule stays the one that is predictable from the dial.

Three consequences worth knowing:

- **`minute(s)` stops appearing** in the rounded modes, because every minute
  becomes a multiple of five and the AUTO rule never fires. That is the
  point of the mode, not a side effect.
- **:58 and :59 roll the hour forward but not the date.** At 23:58 the face
  reads *nearly midnight* above today's date. The date comes from the real
  time; only the phrase is rounded.
- **`twenty-five` is set on ONE line in both rounded modes.** It fits at
  181px — the same 9px right margin `midnight` already lives with. Rounding
  leaves 25 as the only minute that can land in the 21–29 band at all, so
  there is nothing for it to look inconsistent beside, and in the spoken
  mode it frees the row the hedge needs. The exact mode still has to split,
  since `twenty-seven` is 206px and would run off the screen.
- **The two rounded modes are one rigid translation apart.** They say the
  same thing; the spoken one merely admits which way it rounded. So they
  draw the same words on the same rows at the same indents, and differ only
  in that the spoken phrase sits lower to leave the hedge its room. Two
  block levers, one per mode, so tuning either never moves the other.
  This holds on the hour too, since `place_centred()` stopped the hedge
  moving the wording it qualifies.
- **The hedge never moves the words it qualifies.** In the stacked layout it
  never could - the phrase hangs off `REL_TOP`. In the centred layouts it
  did: `centre()` was centring the hedge along with the `o'clock` wording,
  so at :00 with no hedge and :01 with one, `midday` sat 15px apart. Nothing
  about the phrase had changed but every `top` had, and `same()` compares
  `top`, so the whole reveal played again over words that were already on
  the screen and already right. `place_centred()` centres the words alone
  and hangs the hedge above them at one row pitch, so only the hedge itself
  animates in and out. Worth about 44 fewer word-redraws a day in the spoken
  mode, and the on-the-hour wording now holds perfectly still.

The hedge sits **flat** above the word it qualifies rather than a step in:
it modifies that word instead of being a rung of its own, and at one indent
`twenty-five` would end exactly on the right edge.

The witching hour answers to the **real** clock, not the spoken one. Left on
the rounded time it fired for five minutes either side of three, which both
diluted a once-a-day easter egg and did not fit — three SOLO rows plus a
hedge span 203px against the 194 available, and the sweep caught the top row
clipped by 4px. So 02:58 reads *nearly three o' clock* in the ordinary way.

**Whether `minute(s)` is spoken is a setting** (`kMinutesModes`, chosen on
the phone). `mins` below is the *spoken* minute count — 29 at both `:29`
and `:31` — not `tm_min`:

```
0  auto    mins % 5 != 0        "twenty-nine minutes to ten", "five past five"
1  never   never                "twenty-nine to ten",         "five past five"
2  always  mins != 15, 30       "twenty-nine minutes to ten", "five minutes past five"
```

Mode 2 stops at 15 and 30 because those are the two minutes spoken as a
*word* rather than a number — "quarter minutes past" and "half minutes
past" are not English, and no amount of "always" makes them so. 45 needs no
special case: it is spoken as *quarter to*, with `mins == 15`.

The modes cost nothing structurally. Where `minutes` appears on a non-split
word it takes its own row, and beside a split word it rides inline — both
shapes already existed, so mode 2 introduces no new row count and no new
vertical extent, and mode 1 only removes rows. Confirmed by sweeping all
three modes against both date formats (§5), not assumed.

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
cases — **once per date format × minutes mode**, driven off
`DATE_FORMAT_COUNT` and `MINUTES_MODE_COUNT` so a new setting value is swept
without anyone remembering to widen a loop. Around 3.2 million assertions,
under a second.

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
| minutes modes | presence, absence and singular/plural of `minute(s)` asserted from the spec wording rather than from a copy of the predicate, plus the specific phrases the setting promises (*five minutes past five*, *quarter past one*) |
| message routing | each settings key drives the field it names and no other, both tuple types, clamping applied on the way in |
| block levers | in each rounded mode every row that hangs off `REL_TOP` shifts by exactly that mode's lever, and the hedge, the date and the centred layouts do not move; in the exact mode **nothing** moves — with the membership written out by hand in the test rather than read from the code |
| rounded modes agree | mode 1 and mode 2 draw the same elements, in the same order, at the same x, once the hedge is set aside; every phrase row shifts by the *same* amount as every other; and that amount is exactly the difference between the two block levers. Swept over seven lever pairs, so an equal pair cannot make it pass by collapsing every delta to zero |
| reachability | the C sweep records which rows each combination of reading mode, `minutes` wording and date toggle actually draws, and writes `tools/test/reachability.json`; the settings-page test then drives the real greying handler across the same 18 combinations and requires the enabled controls to match exactly, both directions |
| ink overlap | no descender passes more than `INK_OVERLAP_MAX_PCT` of the ascender below it, none reaches the neighbouring x-height, and no ascender reaches the baseline above — on drawn positions, every minute |
| on the hour | every word in the centred layouts is `SOLO`-sized and on `ROW_SOLO` |
| rounding | the spoken minute is always a multiple of five and never more than two minutes from the real one, and the hedge agrees with the direction it moved |
| hedge | first in reading order, flush with the margin, never a step in, never outside the spoken mode |
| baked baseline | with every slider at zero each row draws at exactly its `config.h` offset, in all three reading modes, and a slider ADDS to that rather than replacing it — with the expected values transcribed from `config.h` rather than read back from `baked_offset()` |
| hedge rub-out | only a hedge is ever erased, only when the new face has none, never outside the spoken mode, and never when one hedge merely replaces another — with the expectation computed from the previous face rather than from `collect_leaving()`. The rub-out itself is checked on the DRAWN RECTANGLES: left edge pinned, width shrinking to nothing. 287 erasures a day |
| hedge holds still | across all 46 on-the-hour transitions, the hedged face and the un-hedged one place every other word at the same row, x and top — asserted on POSITION, not on the animation flags, because position is the cause and a reveal rewritten to paper over a moving layout would still pass a flag check |
| drawn offsets | every element moves by exactly its own row's offset — no row drags a neighbour, no slider fails to reach its row — swept over 25 offset combinations, plus the two halves of a split number never sharing a row |
| palette | across five colour schemes including colour-on-colour: level 0 transparent, level 15 exactly the ink, the ramp monotonic, and the bold outline invisible on light ink but inked on dark |
| settings | `tuple_int()` reads Clay's cstring **and** int tuples; an out-of-range index falls back rather than indexing off `kDateFormats` or `kMinutesModes` |
| memory | the reveal is played out frame by frame, so `prune_cache()` and the sub-bitmap slicing run for real under ASAN |

The suite is only worth what it catches, so it has been checked against
regressions rather than assumed: re-introducing the `kOnes[19]` read, the
indent-by-position bug, the appended-`minutes` reveal-order bug, an
unpinned relation row, a flattened ordinal, and a `prune_cache()` that
stops freeing all produce failures. So do, since the date formats landed:
an off-by-one `kWeekdays` index, a gap emitted before the first atom
instead of only between atoms, `make_tm()` leaving `tm_wday` at zero, a
"never" mode that wasn't, an "always" mode that said *quarter minutes past*,
a `minutes` that never went singular, an inverted ink/paper
polarity test, a bold outline shown on light ink, one never shown on dark
ink, the two halves of a split number sharing a row again, a settings key
routed to the wrong field, a key not read at all, clamping dropped from the
incoming message, each of the minute number's three cases folded back
onto a shared lever, rows crammed past the overlap rule, the hour row shoved
into the date, the split head pushed off the top of the screen, the
on-the-hour words reverted to `TIME` size, the rounding direction inverted,
rounding to the nearest ten, the hedge taking an indent step, the hedge
leaking into the plain rounded mode, `twenty-five` set on one line in
every mode, the block lever dragging the date or skipping the hour row, the
two hedge levers wired to one field, and the centred layouts using the
stacked hedge row. And since the second block lever landed: `twenty-five`
reverted to splitting in the plain rounded mode, the relation row dropped
out of the block so the phrase stretched instead of translating, the two
rounded modes sharing one lever, the new lever wired to nothing, the
settings page greying the two block levers the wrong way round, the greying
ignoring the date toggle, and the greying inverted outright. And returning
the hedge to the centred group, which moves the o'clock wording 15px and
forces the redraw that started all this. Since the rub-out: erasing from the
left, leaving the erase out of the stroke budget so it never finishes,
erasing a hedge that was only being replaced, erasing in every reading mode,
sliding the slice sideways instead of shortening it in place, and letting
`prune_cache()` free the word mid-erase. And since the baseline was baked
in: ignoring it so the sliders are absolute again, dropping it from one row,
seeding the sliders with it so every offset doubles, and a slider replacing
the baseline instead of adding to it.

**Two tests that could not fail, both found by injection.** The block
lever's check computed what it expected by calling `in_phrase_block()` — the
function under test — so adding the date to the phrase, or dropping the hour
row from it, both stayed green. The membership is now written out by hand in
the harness, with a separate assertion that the two agree, so a change to
the real one has to be argued for against the list. Separately, that same
check compared a *drawn* baseline against an *unoffset* one, folding every
row's own offset into the delta and failing 20,000 correct faces.

**The budget checks were measuring a layout nobody sees.** `check_fit`,
both `_Static_assert`s and `check_bounds` all worked on the computed
positions, before the row offsets are applied. With `OFFSET_HOUR` at −9 that
reported the hour row colliding with the date while it sat 9px clear of it
on screen, and it refused a font size that fits perfectly well. All four now
work on drawn positions. The offsets are part of the layout, not a
decoration applied afterwards.

That last family is worth singling out. **Twice** a case was quietly put
back on a shared row and the entire suite stayed green**,** because "every
row moves by exactly its own offset" cannot see a word assigned to the wrong
row — whichever row it is on, it moves by that row's offset, perfectly
consistently. Consistency was never the property at risk; reachability was.
The fix each time was to assert directly which lever a case answers to.

Two of those injections initially produced no failures at all — and no test
output either, because removing the polarity test left `dark_ink` unused and
`-Werror` stopped the build. A broken build is detection of a sort, but it
is not the assertion firing, and reading "no FAIL lines" as "the test
passed" would have been exactly the wrong conclusion. Injections have to
compile to prove anything.
Hold future changes to that standard — if a change cannot break a test,
add the test that it would break.

That last one is worth dwelling on. `make_tm()` never set `tm_wday`, and
nothing noticed for as long as nothing read it. The moment a format showed
the weekday, a `memset`-to-zero `tm` would have made every swept date a
Sunday — and the weekday assertion would have passed all year without ever
being tested. A test that cannot fail is worse than no test, because it is
counted. `sweep_dates()` now also asserts that all seven weekdays actually
appeared.

**The settings page has its own runner.** `tools/test/clay-slider.test.js`
drives the real `src/pkjs/custom-clay.js` against a fake DOM — same
principle, different fake. `run.sh` runs it when `node` is present and skips
it with a note when it is not; node is not a build dependency and must not
become one. It too has been checked against injected regressions: a missing
thumb-travel correction, a value that is never restored, a forgotten
`stopPropagation`, and a cancelled `touchstart`.

The first version of that test did *not* catch the thumb-travel injection —
its two extreme cases happened to fall inside the slop of both the right
geometry and a naive one, so it agreed with either. Worth recording,
because the test looked thorough and was not: fail-injection is what
distinguishes the two, and this is the case where it earned its place.

One limit of the palette checks is worth writing down. "Level 15 is exactly
the ink" cannot detect a small drift, because levels 13–15 all quantise to
the same colour on a screen with four levels per channel — pushing level 15
down to 14 changes nothing observable, and the test rightly stays green. It
only fires once the drift crosses a quantisation step. That is a property of
the hardware rather than a gap in the test, but it means the assertion is
weaker than it reads.

Three things the suite does **not** cover: anything about how the words
actually look (colour, antialiasing, stroke weight — all of §2's palette
work); the vertical budget arithmetic, which lives in `tune.py` and the
`_Static_assert`s instead; and anything about the settings page that a fake
DOM cannot know — real event ordering in the phone's browser, where a finger
actually lands, whether the page still scrolls. Hardware remains the final
check.

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

**Dark ink on light paper is better, not settled.** The emboldening outline
(§2) at Medium reads "a little bit clearer" on the watch; Solid is bolder
but the edges go crunchy, which is what a four-level screen does when an
antialiased edge is pushed to full ink. So the current setting is a balance
rather than a match, and the remaining thinness is unresolved.

Deliberately left there for now. The obvious next lever is raising
`BOLD_BLEND` past 0.26 toward 0.40, which targets white-on-black's apparent
weight instead of the nominal one — but the whole question changes with v2
(§7), because hand-drawn strokes will have their own weight and the
rendered-font measurements stop applying. Worth revisiting then rather than
tuning twice.

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
