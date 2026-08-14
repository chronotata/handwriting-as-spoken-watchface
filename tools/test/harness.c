/*
 * Layout tests for the Handwritten As Spoken watchface.
 *
 *     tools/test/run.sh
 *
 * This compiles the REAL src/c/handwritten.c against a stub of the Pebble SDK
 * (tools/test/pebble.h) and sweeps its own build_face() over every minute of
 * the day, plus every day of a leap year for the date line. It is not a model
 * of the layout - it is the layout.
 *
 * That distinction earned its keep: the ":20 and :40 show one instead of
 * twenty" bug was an out-of-bounds read of kOnes[19], which is undefined
 * behaviour rather than a crash, so it drew a plausible wrong word instead of
 * failing. A Python re-implementation would have had its own array and would
 * have been silently correct. Here, -fsanitize=address catches it outright.
 *
 * Adding a check: write it as check_*(), call it from sweep_minutes() or
 * sweep_dates(), and report through fail(). Everything is data-driven off
 * WORDS[] and WORD_INK[], so nothing needs updating when tune.py regenerates.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "generated.h"   /* RESOURCE_ID_*, WORD_INK[] - written by tune.py */

/* handwritten.c owns main(); the harness needs its own. */
#define main watchface_main
#include "../../src/c/handwritten.c"
#undef main

/* ------------------------------------------------------------------ */
/* Reporting                                                           */
/* ------------------------------------------------------------------ */

static int s_failures;
static int s_checks;

static void fail(const char *what, int h, int m, const char *detail) {
  if (s_failures < 25) {
    fprintf(stderr, "  FAIL %02d:%02d  %s: %s\n", h, m, what, detail);
  } else if (s_failures == 25) {
    fprintf(stderr, "  ... further failures suppressed\n");
  }
  s_failures++;
}

#define CHECK(cond, what, h, m, detail)   \
  do {                                    \
    s_checks++;                           \
    if (!(cond)) fail(what, h, m, detail); \
  } while (0)

/* ------------------------------------------------------------------ */
/* Helpers over the face                                               */
/* ------------------------------------------------------------------ */

static int baseline_of(const Element *e) { return e->top + WORDS[e->word].base; }
static int ink_top_of(const Element *e) {
  return baseline_of(e) - WORD_INK[e->word].asc;
}
static int ink_bottom_of(const Element *e) {
  return baseline_of(e) + WORD_INK[e->word].desc;
}

/*
 * The same three, WHERE THE WORD IS DRAWN. Bounds used to be checked without
 * the row offsets, which measured a layout nobody sees: with OFFSET_HOUR at
 * -9 the hour row was reported colliding with the date while sitting 9px
 * clear of it on screen.
 */
static int drawn_baseline_of(const Element *e) {
  return baseline_of(e) + row_offset(e->row);
}
static int drawn_ink_top(const Element *e) {
  return drawn_baseline_of(e) - WORD_INK[e->word].asc;
}
static int drawn_ink_bottom(const Element *e) {
  return drawn_baseline_of(e) + WORD_INK[e->word].desc;
}

/*
 * Sakamoto's algorithm. 0 = Sunday, matching struct tm's tm_wday.
 *
 * Done arithmetically rather than through mktime() so the sweep stays
 * deterministic: mktime() consults the host timezone, which would make the
 * weekday - and therefore the whole date line in format 2 - depend on where
 * the tests happen to be run.
 */
static int day_of_week(int year, int mon0, int mday) {
  static const int kShift[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int y = year;
  if (mon0 < 2) {
    y -= 1;
  }
  return (y + y / 4 - y / 100 + y / 400 + kShift[mon0] + mday) % 7;
}

/*
 * tm_wday MUST be filled in here. It was not, originally, and nothing noticed
 * because nothing read it - but the moment a date format shows the weekday, a
 * memset-to-zero tm makes every single swept date a Sunday and the weekday
 * assertion passes without ever having been tested.
 */
static struct tm make_tm(int yday, int mday, int mon, int year, int h, int m) {
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_hour = h; t.tm_min = m;
  t.tm_mday = mday; t.tm_mon = mon; t.tm_year = year - 1900; t.tm_yday = yday;
  t.tm_wday = day_of_week(year, mon, mday);
  return t;
}

/* ------------------------------------------------------------------ */
/* Checks                                                              */
/* ------------------------------------------------------------------ */

/* Nothing may be drawn off any edge, and no time row may reach into the
 * date's ascenders. Checked against INK, not canvases: canvases are the
 * family box and legitimately hang past the top of the screen (the tallest
 * phrase starts at y = -6), but the letters inside must not. */
static void check_bounds(const Face *f, int h, int m) {
  char buf[170];
  int date_ink_top = SCREEN_H;
  for (int i = 0; i < f->count; i++) {
    if (f->items[i].row == ROW_DATE) {
      int t = drawn_ink_top(&f->items[i]);
      if (t < date_ink_top) date_ink_top = t;
    }
  }
  for (int i = 0; i < f->count; i++) {
    const Element *e = &f->items[i];
    const WordGeom *g = &WORDS[e->word];

    snprintf(buf, sizeof(buf), "element %d (word %d) x=%d w=%d", i, e->word,
             e->x, g->w);
    CHECK(e->x >= 0 && e->x + g->w <= SCREEN_W, "off screen horizontally", h, m, buf);

    snprintf(buf, sizeof(buf), "element %d (word %d) ink %d..%d", i, e->word,
             drawn_ink_top(e), drawn_ink_bottom(e));
    CHECK(drawn_ink_top(e) >= 0, "ink clipped at top", h, m, buf);
    CHECK(drawn_ink_bottom(e) <= SCREEN_H, "ink clipped at bottom", h, m, buf);

    if (e->row != ROW_DATE) {
      snprintf(buf, sizeof(buf), "element %d ink bottom %d, date ink top %d",
               i, drawn_ink_bottom(e), date_ink_top);
      CHECK(drawn_ink_bottom(e) < date_ink_top, "time row collides with date", h, m, buf);
    }
  }
}

/* Relation and hour are pinned to indent 2 and 3 by ROLE, whatever sits above
 * them. Indenting by array position instead made "past"/"one" shift sideways
 * when a "minutes" row appeared, redrawing two words that had not changed. */
static void check_indents(const Face *f, int h, int m) {
  char buf[120];
  if (m == 0) {
    return;   /* the o'clock, solo and witching layouts indent by position */
  }
  CHECK(f->items[0].x == MARGIN, "first row not at margin", h, m, "");
  for (int i = 0; i < f->count; i++) {
    const Element *e = &f->items[i];
    if (e->row == ROW_RELATION) {
      snprintf(buf, sizeof(buf), "x=%d expected %d", e->x, MARGIN + 2 * INDENT);
      CHECK(e->x == MARGIN + 2 * INDENT, "relation not at indent 2", h, m, buf);
    }
    if (e->row == ROW_HOUR) {
      snprintf(buf, sizeof(buf), "x=%d expected %d", e->x, MARGIN + 3 * INDENT);
      CHECK(e->x == MARGIN + 3 * INDENT, "hour not at indent 3", h, m, buf);
    }
  }
}

/* Stacked canvases sit exactly ROW_GAP apart.
 *
 * The "minutes" annotation riding inline beside a split word is NOT part of
 * the stack - it hangs off its host's baseline and sits between two stacked
 * rows in the array. So it is skipped rather than compared: the row below it
 * stacks on its HOST, not on it. */
static void check_stacking(const Face *f, int h, int m) {
  char buf[160];
  const Element *prev = NULL;
  for (int i = 0; i < f->count; i++) {
    const Element *e = &f->items[i];
    if (e->row == ROW_DATE) {
      continue;
    }
    if (prev && e->row == ROW_MINUTES_INLINE) {
      continue;   /* inline beside its host; leaves the stack chain alone */
    }
    if (prev) {
      int want = prev->top + WORDS[prev->word].h + ROW_GAP;
      snprintf(buf, sizeof(buf), "element %d top %d, expected %d", i, e->top, want);
      CHECK(e->top == want, "row gap is not ROW_GAP", h, m, buf);
    }
    prev = e;
  }
}

/* Array order is animation order, so it must equal reading order. The split
 * "minutes" is computed last (its position depends on its neighbour) but
 * inserted third, which is the bug this guards. */
static void check_reading_order(const Face *f, int h, int m) {
  char buf[160];
  for (int i = 0; i + 1 < f->count; i++) {
    const Element *a = &f->items[i], *b = &f->items[i + 1];
    if (a->row == ROW_DATE || b->row == ROW_DATE) {
      continue;
    }
    snprintf(buf, sizeof(buf), "element %d top %d then %d top %d",
             i, a->top, i + 1, b->top);
    CHECK(b->top > a->top || (b->top == a->top && b->x > a->x),
          "reveal order is not reading order", h, m, buf);
  }
}

/* The whole point of uniform family boxes: the relation and hour rows hold
 * one position for an entire half hour, and every time row lands on the same
 * baseline grid. If this drifts, rows redraw for no reason. */
static void check_pinned_rows(const Face *f, int h, int m) {
  char buf[140];
  if (m == 0) {
    return;
  }
  for (int i = 0; i < f->count; i++) {
    const Element *e = &f->items[i];
    if (e->row == ROW_RELATION) {
      snprintf(buf, sizeof(buf), "top=%d expected %d", e->top, REL_TOP);
      CHECK(e->top == REL_TOP, "relation row not pinned at REL_TOP", h, m, buf);
    }
    if (e->row == ROW_HOUR) {
      int want = REL_TOP + TIME_BOX_H + ROW_GAP;
      snprintf(buf, sizeof(buf), "top=%d expected %d", e->top, want);
      CHECK(e->top == want, "hour row not one pitch below relation", h, m, buf);
    }
  }
}

/* Date atoms all sit on one fixed baseline regardless of month, so months
 * without descenders do not ride higher than months with them. The ordinal
 * is raised by its own baseline, not by aligning canvas tops. */
static void check_date_baseline(const Face *f, int h, int m) {
  char buf[140];
  int lo = SCREEN_W, hi = 0;
  for (int i = 0; i < f->count; i++) {
    const Element *e = &f->items[i];
    if (e->row != ROW_DATE) {
      continue;
    }
    bool ordinal = (e->word == W_ST || e->word == W_ND ||
                    e->word == W_RD || e->word == W_TH);
    int want = ordinal ? DATE_BASELINE - ORD_RISE : DATE_BASELINE;
    snprintf(buf, sizeof(buf), "word %d baseline %d expected %d",
             e->word, baseline_of(e), want);
    CHECK(baseline_of(e) == want, "date atom off its baseline", h, m, buf);
    if (e->x < lo) lo = e->x;
    if (e->x + WORDS[e->word].w > hi) hi = e->x + WORDS[e->word].w;
  }
  if (hi > 0) {
    snprintf(buf, sizeof(buf), "spans %d..%d on a %d screen", lo, hi, SCREEN_W);
    CHECK(abs(lo - (SCREEN_W - hi)) <= 1, "date line not centred", h, m, buf);
  }
}

/*
 * The format-1 date builder EXACTLY as it stood before the atom refactor,
 * transcribed rather than reimplemented: identity-based spacing ("a space
 * after the ordinal and after the month"), the hard-coded DATE_SPACE * 2 in
 * the total, the original centring.
 *
 * This is the oracle for the refactor. The new code derives spacing from atom
 * POSITION instead, which is a different rule that happens to agree for this
 * one format - "happens to agree" is precisely the sort of claim that needs
 * checking against the thing it replaced, not against itself. Once a third
 * format exists this function stays frozen: it describes format 1 only.
 */
static void legacy_format1(const struct tm *t, uint8_t *word, int16_t *xs,
                           int16_t *tops, int *out_n) {
  const int day = t->tm_mday;
  const uint8_t d1 = (day >= 10) ? kDigits[day / 10] : 0xFF;
  const uint8_t d2 = kDigits[day % 10];
  const uint8_t suf = ordinal_word(day);
  const uint8_t mon = kMonths[t->tm_mon];
  const int year = 1900 + t->tm_year;
  const uint8_t y[4] = {
    kDigits[(year / 1000) % 10], kDigits[(year / 100) % 10],
    kDigits[(year / 10) % 10], kDigits[year % 10]
  };

  uint8_t seq[8];
  int n = 0;
  if (d1 != 0xFF) {
    seq[n++] = d1;
  }
  seq[n++] = d2;
  seq[n++] = suf;
  seq[n++] = mon;
  for (int i = 0; i < 4; i++) {
    seq[n++] = y[i];
  }

  int total = 0;
  for (int i = 0; i < n; i++) {
    total += WORDS[seq[i]].w;
  }
  total += DATE_SPACE * 2;

  int x = (SCREEN_W - total) / 2;
  for (int i = 0; i < n; i++) {
    word[i] = seq[i];
    xs[i] = (int16_t)x;
    tops[i] = (int16_t)((seq[i] == suf)
                        ? DATE_BASELINE - ORD_RISE - WORDS[suf].base
                        : DATE_BASELINE - WORDS[seq[i]].base);
    x += WORDS[seq[i]].w;
    if (seq[i] == suf || seq[i] == mon) {
      x += DATE_SPACE;
    }
  }
  *out_n = n;
}

/* Format 1 must come out of the refactored builder bit for bit. */
static void check_format1_unchanged(const Face *f, const struct tm *t,
                                    int h, int m) {
  char buf[180];
  uint8_t word[8];
  int16_t xs[8], tops[8];
  int want_n = 0;
  legacy_format1(t, word, xs, tops, &want_n);

  int got = 0;
  for (int i = 0; i < f->count; i++) {
    const Element *e = &f->items[i];
    if (e->row != ROW_DATE) {
      continue;
    }
    if (got < want_n) {
      snprintf(buf, sizeof(buf),
               "atom %d: word %d/%d x %d/%d top %d/%d (new/legacy)",
               got, e->word, word[got], e->x, xs[got], e->top, tops[got]);
      CHECK(e->word == word[got] && e->x == xs[got] && e->top == tops[got],
            "refactor changed format 1", h, m, buf);
    }
    got++;
  }
  snprintf(buf, sizeof(buf), "%d date elements, legacy built %d", got, want_n);
  CHECK(got == want_n, "refactor changed format 1 element count", h, m, buf);
}

/* Format 2: the weekday is right, and the year is gone. */
static void check_weekday_format(const Face *f, const struct tm *t,
                                 int h, int m) {
  char buf[180];
  const int wday = day_of_week(1900 + t->tm_year, t->tm_mon, t->tm_mday);
  const uint8_t want = kWeekdays[wday];

  int date_elements = 0, weekdays = 0;
  const Element *first = NULL;
  for (int i = 0; i < f->count; i++) {
    const Element *e = &f->items[i];
    if (e->row != ROW_DATE) {
      continue;
    }
    if (!first) {
      first = e;
    }
    date_elements++;
    for (int d = 0; d < 7; d++) {
      if (e->word == kWeekdays[d]) {
        weekdays++;
        snprintf(buf, sizeof(buf), "showed weekday %d, date is weekday %d",
                 d, wday);
        CHECK(e->word == want, "wrong weekday", h, m, buf);
      }
    }
  }

  CHECK(weekdays == 1, "format 2 should carry exactly one weekday", h, m, "");
  if (first) {
    snprintf(buf, sizeof(buf), "first date element is word %d", first->word);
    CHECK(first->word == want, "weekday is not first in reading order",
          h, m, buf);
  }

  /* weekday + day digits + ordinal + month, and no year. */
  const int want_n = 1 + ((t->tm_mday >= 10) ? 2 : 1) + 1 + 1;
  snprintf(buf, sizeof(buf), "%d date elements, expected %d (year present?)",
           date_elements, want_n);
  CHECK(date_elements == want_n, "format 2 element count", h, m, buf);
}

/*
 * The "minute(s)" annotation, stated as the SPEC says it rather than as the
 * code computes it.
 *
 * wants_minutes() is a three-line predicate, so a harness copy of it would
 * be the "model agrees with itself" trap in miniature - it would pass by
 * construction. These assertions are written from the wording the setting
 * promises instead: never; always except the two word-minutes; only on
 * non-multiples of five. If someone changes the predicate, these have to be
 * re-argued from the spec, which is the point.
 */
static void check_minutes_mode(const Face *f, uint8_t mode, int h, int m) {
  char buf[180];

  int found = 0;
  uint8_t which = 0;
  for (int i = 0; i < f->count; i++) {
    if (f->items[i].word == W_MINUTE || f->items[i].word == W_MINUTES) {
      found++;
      which = f->items[i].word;
    }
  }

  snprintf(buf, sizeof(buf), "%d of them", found);
  CHECK(found <= 1, "more than one \"minutes\"", h, m, buf);

  /* Everything here is about the minute the face SPEAKS, which is not the
   * wall-clock minute once rounding is on. Comparing against m would have
   * this failing all over the rounded modes for a face that is correct. */
  int sh = h, sm = m;
  uint8_t hedge;
  round_clock(&sh, &sm, &hedge);

  /* On the hour there is no minute number to annotate, in any mode. */
  if (sm == 0) {
    CHECK(found == 0, "\"minutes\" on the hour", h, m, kMinutesModes[mode]);
    return;
  }

  const int mins = (sm <= 30) ? sm : 60 - sm;
  bool want;
  switch (mode) {
    case MINS_NEVER:
      want = false;
      break;
    case MINS_ALWAYS:
      /* "quarter" and "half" are the words; everything else is a number. */
      want = (mins != 15 && mins != 30);
      break;
    default:
      want = (mins % 5) != 0;
      break;
  }

  snprintf(buf, sizeof(buf), "mode \"%s\", spoken minute %d: %s, expected %s",
           kMinutesModes[mode], mins, found ? "shown" : "absent",
           want ? "shown" : "absent");
  CHECK(found == (want ? 1 : 0), "\"minutes\" shown when it should not be "
        "(or vice versa)", h, m, buf);

  if (found) {
    snprintf(buf, sizeof(buf), "spoken minute %d took %s", mins,
             which == W_MINUTE ? "\"minute\"" : "\"minutes\"");
    CHECK(which == (mins == 1 ? W_MINUTE : W_MINUTES),
          "singular/plural", h, m, buf);
  }
}

/*
 * The palette, which is where the emboldening lives.
 *
 * tune.py bakes a slightly bolder outline of each word into levels
 * 1..BOLD_RING_TOP. tint() renders it as paper when the ink is lighter than
 * the paper, and as ink when it is darker - that asymmetry IS the fix, so it
 * is what gets asserted. The stub's bitmaps carry tune.py's own level
 * encoding, so palette[i] comes back describing level i.
 */
static int ink_distance(GColor c, GColor paper) {
  return abs(c.r - paper.r) + abs(c.g - paper.g) + abs(c.b - paper.b);
}

static void check_palette_for(GColor paper, GColor ink, bool dark_ink,
                              const char *why) {
  char buf[190];
  s_settings.paper = paper;
  s_settings.ink = ink;
  drop_all_bitmaps();

  GBitmap *b = bitmap_for(W_THREE);       /* loads and tints */
  GColor *pal = gbitmap_get_palette(b);
  const int full = ink_distance(ink, paper);

  /* Level 0 is the transparent background, and 15 is the ink itself. Neither
   * may drift, whatever else the weighting does. */
  CHECK(pal[0].a == 0, "level 0 is not transparent", 0, 0, why);
  snprintf(buf, sizeof(buf), "%s: level 15 argb %02x, ink %02x",
           why, pal[15].argb, ink.argb);
  CHECK(pal[15].argb == ink.argb, "level 15 is not exactly the ink", 0, 0, buf);

  /* The word itself - levels above the outline - must climb steadily from
   * paper to ink, or an antialiased edge would read as a ridge. */
  for (int l = BOLD_RING_TOP + 2; l <= 15; l++) {
    snprintf(buf, sizeof(buf), "%s: level %d is %d from paper, level %d is %d",
             why, l, ink_distance(pal[l], paper), l - 1,
             ink_distance(pal[l - 1], paper));
    CHECK(ink_distance(pal[l], paper) >= ink_distance(pal[l - 1], paper),
          "the paper-to-ink ramp is not monotonic", 0, 0, buf);
  }

  /* And the outline: invisible on light ink, inked on dark ink. */
  for (int l = 1; l <= BOLD_RING_TOP; l++) {
    const int d = (pal[l].a == 0) ? 0 : ink_distance(pal[l], paper);
    snprintf(buf, sizeof(buf), "%s: outline level %d sits %d of %d toward ink",
             why, l, d, full);
    if (dark_ink) {
      CHECK(d * 2 >= full, "the bold outline is not inked on dark ink",
            0, 0, buf);
    } else {
      CHECK(d == 0, "the bold outline is visible on light ink", 0, 0, buf);
    }
  }

  drop_all_bitmaps();
}

static void check_palette(void) {
  const uint8_t weight = s_settings.stroke_weight;
  s_settings.stroke_weight = DEFAULT_STROKE_WEIGHT;

  check_palette_for(GColorBlack, GColorWhite, false, "white ink on black");
  check_palette_for(GColorWhite, GColorBlack, true,  "black ink on white");
  /* Colour on colour, which is why the test is luminance-based and not a
   * black/white special case. */
  check_palette_for(GColorFromHEX(0xFFFF00), GColorFromHEX(0x000055), true,
                    "dark blue ink on yellow");
  check_palette_for(GColorFromHEX(0x000055), GColorFromHEX(0xFFFF00), false,
                    "yellow ink on dark blue");

  /* Weight 0 must switch the emboldening off even on dark ink - that is the
   * escape hatch if the compensation is ever wrong for someone's screen. */
  s_settings.stroke_weight = 0;
  check_palette_for(GColorWhite, GColorBlack, false,
                    "black on white, stroke weight 0");

  s_settings.stroke_weight = weight;
  drop_all_bitmaps();
  printf("  palette schemes      5\n");
}

/*
 * THE BLOCK LEVER moves the phrase and nothing else.
 *
 * It is the one offset that touches many rows at once, so the rule it has to
 * obey is a relationship rather than a position: every row that hangs off
 * REL_TOP shifts by exactly it, and the hedge, the date and the centred
 * layouts do not move at all. Those are anchored elsewhere, and holding them
 * still is the entire point - the lever exists to shift the phrase
 * RELATIVE to them.
 *
 * It applies ONLY in the spoken reading mode: the other two layouts are
 * already balanced and must not shift, so the same slider set to the same
 * value has to move nothing at all in those. That is asserted here too,
 * since "moves the phrase" and "moves the phrase only sometimes" are
 * different rules and only one of them is the one wanted.
 *
 * The membership below is written out BY HAND rather than asking
 * in_phrase_block(). Calling the function under test to work out what it
 * should have done makes the check a tautology: the first version did
 * exactly that, and adding the date to the phrase, or dropping the hour row
 * from it, both stayed green. Listing the rows here means a change to the
 * real one has to be argued for against this one.
 */
static bool expected_in_block(uint8_t row) {
  return row == ROW_SPLIT_HEAD || row == ROW_MINUTE ||
         row == ROW_MINUTE_ALONE || row == ROW_MINUTE_SPLIT ||
         row == ROW_MINUTES_OWN || row == ROW_MINUTES_INLINE ||
         row == ROW_RELATION || row == ROW_HOUR;
}

/*
 * THE HEDGE MUST NOT MOVE THE WORDS IT QUALIFIES.
 *
 * On the hour the hedge comes and goes minute by minute - "just gone midday"
 * at :01, plain "midday" at :00, "nearly midday" at 11:58. If its arrival
 * shifts the wording, every one of those words counts as a new element and
 * the whole reveal plays again, redrawing words that were already on screen
 * and already right. Which is what happened: centre() was centring the
 * hedge along with them, so "midday" moved 15px and the face redrew itself.
 *
 * Asserted on POSITION rather than on the animation flags, because position
 * is the cause. same() compares word, row, x and top, so holding those
 * equal is what makes the redraw go away; checking `animate` instead would
 * pass just as well if the reveal logic were changed to paper over a layout
 * that still moved.
 *
 * The witching hour is exempt and has to be: 03:00 draws three rows of its
 * own easter egg while 03:01 draws an ordinary "just gone three o' clock",
 * so those two legitimately differ in every word. That is deliberate - see
 * the note beside `witching` in handwritten.c.
 */
static void check_hedge_does_not_move_the_hour(void) {
  char buf[220];
  int compared = 0;

  for (int h = 0; h < 24; h++) {
    if (h == WITCHING_HOUR) {
      continue;
    }
    /* :00 exactly - no rounding happened, so no hedge. */
    settings_defaults();
    s_settings.rounding = ROUND_SPOKEN;
    struct tm tm = make_tm(232, 21, 7, 2026, h, 0);
    s_have_prev = false;
    refresh(&tm, true);
    const Face plain = s_face;

    /* :01 rounds back to :00 and says "just gone"; the minute before :00 by
     * two rounds forward to it and says "nearly". Both draw the same hour. */
    const int kHedged[][2] = {
      { h, 1 },
      { (h + 23) % 24, 58 }
    };

    for (size_t k = 0; k < sizeof(kHedged) / sizeof(kHedged[0]); k++) {
      const int hh = kHedged[k][0], mm = kHedged[k][1];
      /* No witching-hour guard needed here: both source times are at :01
       * and :58, and the easter egg keys off the REAL clock reading exactly
       * :00, so neither can draw it. Only the hour they round TO could be
       * the witching hour, and the loop above has already skipped that. */
      settings_defaults();
      s_settings.rounding = ROUND_SPOKEN;
      struct tm t2 = make_tm(232, 21, 7, 2026, hh, mm);
      s_have_prev = false;
      refresh(&t2, true);

      int hedges = 0, j = 0;
      for (int i = 0; i < s_face.count; i++) {
        const Element *e = &s_face.items[i];
        if (e->row == ROW_HEDGE || e->row == ROW_HEDGE_SOLO) {
          hedges++;
          continue;
        }
        if (j >= plain.count) {
          break;
        }
        const Element *p = &plain.items[j++];
        snprintf(buf, sizeof(buf),
                 "%02d:00 has word %d at x %d top %d; %02d:%02d has word %d "
                 "at x %d top %d",
                 h, p->word, p->x, p->top, hh, mm, e->word, e->x, e->top);
        CHECK(e->word == p->word && e->row == p->row && e->x == p->x
                && e->top == p->top,
              "the hedge moved the wording it qualifies, forcing a redraw",
              hh, mm, buf);
      }
      snprintf(buf, sizeof(buf), "%02d:%02d drew %d hedges and %d other words,"
               " against %d at %02d:00", hh, mm, hedges, j, plain.count, h);
      CHECK(hedges == 1, "expected exactly one hedge", hh, mm, buf);
      CHECK(j == plain.count, "the hedge changed how many words are drawn",
            hh, mm, buf);
      compared++;
    }
  }
  printf("  hedge holds still    %d transitions\n", compared);
}

/* ------------------------------------------------------------------ */
/* Reachability                                                        */
/* ------------------------------------------------------------------ */

/*
 * WHICH SETTINGS CAN DO ANYTHING, GIVEN THE OTHER SETTINGS.
 *
 * A lever for a row that the current combination of modes never draws is a
 * control that does nothing, and offering it invites the reasonable
 * conclusion that the watchface is broken. So the settings page greys those
 * out - but "which levers are dead right now" is a fact about the LAYOUT
 * code, and the settings page is JavaScript that cannot see it.
 *
 * Rather than keep a hand-written list in the page and hope it stays true,
 * this sweeps every combination, records which rows are ever drawn, and
 * writes the answer out. tools/test/clay-slider.test.js then drives the real
 * settings-page handler across the same combinations and fails if it greys
 * out anything different. The layout is the source of truth; the page has to
 * agree with it, and changing what a mode draws forces the page to be
 * revisited rather than silently going stale.
 *
 * Deriving it from the sweep rather than from reading the code also means it
 * reports what the face DOES, not what it was meant to do.
 */
static const char *const kRowKey[ROW_COUNT] = {
  [ROW_MINUTE]        = "OffMinute",
  [ROW_MINUTES_INLINE] = "OffMinutes",
  [ROW_RELATION]      = "OffRelation",
  [ROW_HOUR]          = "OffHour",
  [ROW_SOLO]          = "OffSolo",
  [ROW_DATE]          = "OffDate",
  [ROW_SPLIT_HEAD]    = "OffSplitHead",
  [ROW_MINUTES_OWN]   = "OffMinutesOwn",
  [ROW_MINUTE_ALONE]  = "OffMinuteAlone",
  [ROW_MINUTE_SPLIT]  = "OffMinuteSplit",
  [ROW_HEDGE]         = "OffHedge",
  [ROW_HEDGE_SOLO]    = "OffHedgeSolo"
};

static void emit_reachability(void) {
  FILE *out = fopen("reachability.json", "w");
  if (!out) {
    printf("  reachability         COULD NOT WRITE reachability.json\n");
    CHECK(false, "reachability.json could not be written", 0, 0,
          "the settings-page test reads it");
    return;
  }
  fprintf(out, "{\n");

  int cells = 0;
  for (uint8_t rnd = 0; rnd < ROUNDING_COUNT; rnd++) {
    for (uint8_t mode = 0; mode < MINUTES_MODE_COUNT; mode++) {
      for (int date_on = 0; date_on <= 1; date_on++) {
        bool seen[ROW_COUNT];
        memset(seen, 0, sizeof(seen));

        for (uint8_t fmt = 0; fmt < DATE_FORMAT_COUNT; fmt++) {
          for (int t = 0; t < 1440; t++) {
            struct tm tm = make_tm(232, 21, 7, 2026, t / 60, t % 60);
            settings_defaults();
            s_settings.rounding = rnd;
            s_settings.minutes_mode = mode;
            s_settings.date_format = fmt;
            s_settings.show_date = (date_on != 0);
            s_have_prev = false;
            refresh(&tm, true);
            for (int i = 0; i < s_face.count; i++) {
              seen[s_face.items[i].row] = true;
            }
          }
        }

        /* The block levers are not rows. Each moves the phrase in one
         * reading mode, so it is live exactly when that mode is chosen and
         * the phrase is something this combination ever draws. */
        bool phrase = false;
        for (uint8_t r = 0; r < ROW_COUNT; r++) {
          if (seen[r] && expected_in_block(r)) {
            phrase = true;
          }
        }

        if (cells++) {
          fprintf(out, ",\n");
        }
        fprintf(out, "  \"%d|%d|%d\": [", rnd, mode, date_on);

        int n = 0;
        for (uint8_t r = 0; r < ROW_COUNT; r++) {
          if (seen[r] && kRowKey[r]) {
            fprintf(out, "%s\"%s\"", n++ ? ", " : "", kRowKey[r]);
          }
        }
        /* The date FORMAT is as dead as the date's own lever when there is
         * no date on screen - it is a setting for something not drawn, which
         * is the same test every row lever gets. */
        if (seen[ROW_DATE]) {
          fprintf(out, "%s\"DateFormat\"", n++ ? ", " : "");
        }
        if (phrase && rnd == ROUND_SPOKEN) {
          fprintf(out, "%s\"OffBlock\"", n++ ? ", " : "");
        }
        if (phrase && rnd == ROUND_FIVE) {
          fprintf(out, "%s\"OffBlockFive\"", n++ ? ", " : "");
        }
        fprintf(out, "]");
      }
    }
  }

  fprintf(out, "\n}\n");
  fclose(out);
  printf("  reachability         %d combinations written\n", cells);
}

/*
 * THE TWO ROUNDED MODES ARE ONE TRANSLATION APART.
 *
 * "Nearest five" and "nearest five, spoken" say the same thing; the second
 * merely admits which way it rounded. So they draw the same phrase - same
 * words, same rows, same indents - and the only intended difference is that
 * the spoken one carries a hedge above it, and therefore wants the whole
 * phrase sitting a little lower to leave room for it.
 *
 * That is what the two block levers are for, and this is the invariant that
 * keeps them honest. It is deliberately NOT "the two modes look identical",
 * which they must not, and NOT "the words match", which would miss the
 * failure that actually matters: one row sliding relative to its neighbours
 * while the block is being tuned. The phrase has to move as a rigid body.
 *
 * Stated as three separate claims, because they fail for different reasons
 * and a merged message would not say which:
 *
 *   1. the same elements, in the same order, at the same x
 *   2. every phrase row shifted by the SAME amount as every other
 *   3. that amount is exactly the difference between the two levers
 *
 * ON THE HOUR IS EXEMPT FROM 2 AND 3, and the first draft of this check was
 * wrong to demand them. There the hedge is not a row sitting above a stacked
 * phrase; it joins the o'clock wording and centre() centres the GROUP, so
 * adding it necessarily moves the words it was added to - by 15 or 16px, as
 * this promptly reported. That is the layout doing what it is supposed to
 * do, and rewriting it to satisfy a test would have broken the one case
 * already agreed to look right in both modes.
 *
 * Claim 1 still holds there, so it is still asserted: the two modes must
 * agree on WHICH words, in what order, at what indent, even where they
 * disagree about the height. Only the vertical claims stand down, and only
 * for the centred layout.
 *
 * Everything outside the phrase in a stacked layout - the date - is held to
 * moving by exactly zero rather than skipped. "Already right" is worth a
 * check, not an exemption; it is what a mis-wired lever breaks first.
 */
static void check_rounded_modes_agree(void) {
  char buf[240];
  /* Pairs, not single values: with both levers equal every delta is zero and
   * the check passes while proving nothing. Includes the range ends, and one
   * equal pair to confirm the degenerate case really does collapse. */
  static const int8_t kPairs[][2] = {
    {0, 0}, {-9, 0}, {-9, -4}, {5, -7}, {15, -15}, {-15, 15}, {-3, -3}
  };
  const int kPairCount = (int)(sizeof(kPairs) / sizeof(kPairs[0]));

  int flat_five[MAX_ELEMENTS];
  Face five;

  for (int p = 0; p < kPairCount; p++) {
    const int spoken_off = kPairs[p][0];
    const int five_off = kPairs[p][1];
    for (uint8_t mode = 0; mode < MINUTES_MODE_COUNT; mode++) {
      for (int t = 0; t < 1440; t++) {
        const int h = t / 60, m = t % 60;
        struct tm tm = make_tm(232, 21, 7, 2026, h, m);

        settings_defaults();
        s_settings.minutes_mode = mode;
        s_settings.rounding = ROUND_FIVE;
        s_settings.offset_block = (int8_t)spoken_off;
        s_settings.offset_block_five = (int8_t)five_off;
        s_have_prev = false;
        refresh(&tm, true);
        five = s_face;
        for (int i = 0; i < five.count; i++) {
          flat_five[i] = drawn_baseline_of(&five.items[i]);
        }

        settings_defaults();
        s_settings.minutes_mode = mode;
        s_settings.rounding = ROUND_SPOKEN;
        s_settings.offset_block = (int8_t)spoken_off;
        s_settings.offset_block_five = (int8_t)five_off;
        s_have_prev = false;
        refresh(&tm, true);

        /* The hedge is the one element the spoken mode adds. Everything
         * left has to line up with the plain mode one for one. */
        Element bare[MAX_ELEMENTS];
        int bare_drawn[MAX_ELEMENTS];
        int n = 0;
        for (int i = 0; i < s_face.count; i++) {
          const Element *e = &s_face.items[i];
          if (e->row == ROW_HEDGE || e->row == ROW_HEDGE_SOLO) {
            continue;
          }
          bare_drawn[n] = drawn_baseline_of(e);
          bare[n++] = *e;
        }

        snprintf(buf, sizeof(buf),
                 "levers %+d/%+d, minutes mode %d: plain has %d elements, "
                 "spoken has %d once the hedge is set aside",
                 spoken_off, five_off, mode, five.count, n);
        CHECK(n == five.count,
              "the two rounded modes do not draw the same phrase", h, m, buf);
        if (n != five.count) {
          continue;
        }

        /* 1. same elements, same order, same x */
        for (int i = 0; i < n; i++) {
          snprintf(buf, sizeof(buf),
                   "element %d: plain word %d row %d x %d, "
                   "spoken word %d row %d x %d",
                   i, five.items[i].word, five.items[i].row, five.items[i].x,
                   bare[i].word, bare[i].row, bare[i].x);
          CHECK(bare[i].word == five.items[i].word,
                "the rounded modes disagree on a word", h, m, buf);
          CHECK(bare[i].row == five.items[i].row,
                "the rounded modes put a word on a different row", h, m, buf);
          CHECK(bare[i].x == five.items[i].x,
                "the rounded modes indent a word differently", h, m, buf);
        }

        /* 2. one uniform shift across the phrase, 3. equal to the levers.
         *
         * These now hold for the centred layouts too. They did not while the
         * hedge was part of what centre() centred: adding it moved the
         * o'clock wording, so the two modes disagreed about where the hour
         * sat by 15px and this check had to exempt them. place_centred()
         * removed the cause, and the exemption went with it. */
        const int want = spoken_off - five_off;
        int seen = INT_MIN;
        int seen_at = -1;
        for (int i = 0; i < n; i++) {
          const int delta = bare_drawn[i] - flat_five[i];
          if (!expected_in_block(bare[i].row)) {
            snprintf(buf, sizeof(buf),
                     "levers %+d/%+d: element %d (word %d, row %d) moved %d "
                     "between the modes, and nothing outside the phrase "
                     "should move at all",
                     spoken_off, five_off, i, bare[i].word, bare[i].row, delta);
            CHECK(delta == 0,
                  "a row outside the phrase moved between the rounded modes",
                  h, m, buf);
            continue;
          }
          if (seen == INT_MIN) {
            seen = delta;
            seen_at = i;
            continue;
          }
          snprintf(buf, sizeof(buf),
                   "levers %+d/%+d: element %d (word %d, row %d) moved %d, "
                   "but element %d moved %d - the phrase is being stretched, "
                   "not translated",
                   spoken_off, five_off, i, bare[i].word, bare[i].row, delta,
                   seen_at, seen);
          CHECK(delta == seen,
                "the phrase drifted row by row between the rounded modes",
                h, m, buf);
        }
        if (seen != INT_MIN) {
          snprintf(buf, sizeof(buf),
                   "levers %+d/%+d: the phrase moved %d between the modes, "
                   "but the levers differ by %d",
                   spoken_off, five_off, seen, want);
          CHECK(seen == want,
                "the shift between the rounded modes is not the two levers",
                h, m, buf);
        }
      }
    }
  }
  printf("  rounded modes        %d lever pairs x %d minutes modes\n",
         kPairCount, MINUTES_MODE_COUNT);
}

static void check_block_offset(int h, int m, uint8_t fmt, uint8_t mode,
                               uint8_t rnd) {
  char buf[210];
  static const int kShifts[] = {-11, 6, 15};
  struct tm tm = make_tm(232, 21, 7, 2026, h, m);

  for (size_t s = 0; s < sizeof(kShifts) / sizeof(kShifts[0]); s++) {
    settings_defaults();
    s_settings.date_format = fmt;
    s_settings.minutes_mode = mode;
    s_settings.rounding = rnd;
    s_settings.offset_block = 0;
    s_settings.offset_block_five = 0;
    s_have_prev = false;
    refresh(&tm, true);
    const Face flat = s_face;

    /* Record where the flat pass DRAWS each row, not where it computes it.
     * The rows carry their own non-zero defaults, and comparing a drawn
     * baseline against an unoffset one folds those in as if the block lever
     * had caused them - which is exactly the false failure this first
     * produced, every row off by its own offset. */
    int flat_drawn[MAX_ELEMENTS];
    for (int i = 0; i < flat.count; i++) {
      flat_drawn[i] = drawn_baseline_of(&flat.items[i]);
    }

    settings_defaults();
    s_settings.date_format = fmt;
    s_settings.minutes_mode = mode;
    s_settings.rounding = rnd;
    /* BOTH levers move together, so each rounded mode is exercised by its
     * own one and the exact mode has to ignore the pair of them. Setting
     * only the spoken lever would have let the rounded lever be wired to
     * anything at all - or to nothing - without this noticing. */
    s_settings.offset_block = (int8_t)kShifts[s];
    s_settings.offset_block_five = (int8_t)kShifts[s];
    s_have_prev = false;
    refresh(&tm, true);

    CHECK(s_face.count == flat.count, "the block lever changed the layout",
          h, m, "element count");

    for (int i = 0; i < s_face.count && i < flat.count; i++) {
      const Element *e = &s_face.items[i];
      /* Neither lever touches the exact mode - see block_offset(). */
      const int want = (rnd != ROUND_EXACT && expected_in_block(e->row))
                       ? kShifts[s] : 0;
      const int moved = drawn_baseline_of(e) - flat_drawn[i];
      snprintf(buf, sizeof(buf),
               "block %+d: element %d (word %d, row %d) moved %d, expected %d",
               kShifts[s], i, e->word, e->row, moved, want);
      CHECK(moved == want, "the block lever moved the wrong rows", h, m, buf);
    }
  }

  for (uint8_t row = 0; row < ROW_COUNT; row++) {
    snprintf(buf, sizeof(buf), "row %d: code says %d, the test expects %d",
             row, in_phrase_block(row), expected_in_block(row));
    CHECK(in_phrase_block(row) == expected_in_block(row),
          "the phrase-block membership drifted from what the tests expect",
          h, m, buf);
  }

  settings_defaults();
  s_settings.date_format = fmt;
  s_settings.minutes_mode = mode;
  s_settings.rounding = rnd;
}

/*
 * ROUNDING, stated from the rule rather than from the code.
 *
 * The face may only ever speak a multiple of five once rounding is on, the
 * spoken time must be within two minutes of the real one, and the hedge has
 * to agree with the direction it moved. Phrased that way rather than by
 * re-deriving the target, so it is a check on the rule and not a copy of it.
 */
static void check_rounding(int h, int m) {
  char buf[200];
  int sh = h, sm = m;
  uint8_t hedge;
  round_clock(&sh, &sm, &hedge);

  if (s_settings.rounding == ROUND_EXACT) {
    CHECK(sh == h && sm == m && hedge == W_COUNT,
          "exact mode changed the time", h, m, "");
    return;
  }

  snprintf(buf, sizeof(buf), "%02d:%02d spoken as %02d:%02d", h, m, sh, sm);
  CHECK(sm % 5 == 0, "a rounded minute is not a multiple of five", h, m, buf);

  /* how far the clock moved, in minutes, signed */
  int delta = (sh * 60 + sm) - (h * 60 + m);
  if (delta > 720) delta -= 1440;
  if (delta < -720) delta += 1440;
  snprintf(buf, sizeof(buf), "%02d:%02d -> %02d:%02d is %d minutes",
           h, m, sh, sm, delta);
  CHECK(delta >= -2 && delta <= 2, "rounded further than the nearest tick",
        h, m, buf);

  if (s_settings.rounding == ROUND_FIVE) {
    CHECK(hedge == W_COUNT, "the plain rounded mode produced a hedge",
          h, m, "");
    return;
  }

  /* ROUND_SPOKEN: the hedge says which way the clock moved. */
  snprintf(buf, sizeof(buf), "moved %+d, hedge %s", delta,
           hedge == W_COUNT ? "none" :
           (hedge == W_NEARLY ? "nearly" : "just gone"));
  if (delta == 0) {
    CHECK(hedge == W_COUNT, "an exact tick was hedged", h, m, buf);
  } else if (delta > 0) {
    CHECK(hedge == W_NEARLY, "rounding forward did not say \"nearly\"",
          h, m, buf);
  } else {
    CHECK(hedge == W_JUST_GONE, "rounding back did not say \"just gone\"",
          h, m, buf);
  }
}

/*
 * The hedge, where it lands on screen.
 *
 * It must sit FLAT above the word it qualifies rather than a step in - at
 * one indent "twenty-five" ends exactly on the right edge, which is the
 * whole reason that word is set on one line in this mode.
 */
static void check_hedge_placement(const Face *f, int h, int m) {
  char buf[190];
  int hedges = 0;
  for (int i = 0; i < f->count; i++) {
    const Element *e = &f->items[i];
    if (e->row != ROW_HEDGE && e->row != ROW_HEDGE_SOLO) {
      continue;
    }
    hedges++;
    CHECK(i == 0, "the hedge is not first in reading order", h, m,
          "it has to be spoken before the minute");
    CHECK(e->x == MARGIN, "the hedge is not flush with the margin", h, m, "");
    if (i + 1 < f->count) {
      snprintf(buf, sizeof(buf), "hedge at x=%d, the word below at x=%d",
               e->x, f->items[i + 1].x);
      CHECK(f->items[i + 1].x == MARGIN,
            "the hedge took an indent step from the word it qualifies",
            h, m, buf);
    }
    /* The stacked and centred hedges answer to different levers. */
    const bool centred = (i + 1 < f->count &&
                          f->items[i + 1].row == ROW_SOLO);
    snprintf(buf, sizeof(buf), "hedge row %d, next row %d", e->row,
             (i + 1 < f->count) ? f->items[i + 1].row : -1);
    CHECK(e->row == (centred ? ROW_HEDGE_SOLO : ROW_HEDGE),
          "the hedge is on the wrong lever for its layout", h, m, buf);
  }
  CHECK(hedges <= 1, "more than one hedge", h, m, "");
  if (s_settings.rounding != ROUND_SPOKEN) {
    CHECK(hedges == 0, "a hedge outside the spoken mode", h, m, "");
  }
}

/*
 * On the hour, every word is set in the SOLO family.
 *
 * Asserted directly because no geometric rule can see it: a smaller word
 * centred on the screen is a perfectly legal layout, so swapping the SOLO
 * table back for the TIME one breaks nothing measurable and every other
 * check stays green. Which table a case draws from is a decision, and
 * decisions have to be stated. Canvas height stands in for the family - the
 * whole point of the family box is that it is uniform.
 */
static void check_hour_family(const Face *f, int h, int m) {
  char buf[140];
  if (m != 0) {
    return;
  }
  for (int i = 0; i < f->count; i++) {
    const Element *e = &f->items[i];
    if (e->row == ROW_DATE || e->row == ROW_HEDGE ||
        e->row == ROW_HEDGE_SOLO) {
      continue;
    }
    snprintf(buf, sizeof(buf), "word %d has canvas height %d, SOLO_BOX_H is %d",
             e->word, WORDS[e->word].h, SOLO_BOX_H);
    CHECK(WORDS[e->word].h == SOLO_BOX_H,
          "an on-the-hour word is not set in the SOLO family", h, m, buf);
    CHECK(e->row == ROW_SOLO,
          "an on-the-hour word is not on ROW_SOLO", h, m,
          "the centred layouts move as one block");
  }
}

/*
 * INK OVERLAP between stacked rows.
 *
 * ROW_GAP is negative: the canvases overlap on purpose, because a script face
 * reads better interwoven than stacked clear. That trades away the property
 * ROW_GAP used to guarantee - that it was a floor on the visible ink gap - so
 * something has to take over, or "interwoven" quietly becomes "collided".
 *
 * Two rules, both from the eye rather than the arithmetic:
 *   - a descender may reach at most INK_OVERLAP_MAX_PCT of the way past the
 *     ascender below it;
 *   - no ink may reach into the neighbouring line's x-height, where the
 *     lowercase bodies are. That is what actually reads as a collision.
 *
 * Checked on the DRAWN positions, since the offsets are what set the real
 * gaps, and over every minute rather than the handful of phrases that were
 * eyeballed in a mock-up - the worst pair is a deep descender above a tall
 * ascender, which need not occur in any phrase anyone happened to look at.
 */
/* Per-word, but the value is its FAMILY's x-height - "minutes" is set at a
 * different size from the number above it, and judging it by the number's
 * x-height flagged a layout that is perfectly fine. */
static int x_height_of(uint8_t word) { return WORD_XHEIGHT[word]; }

static void check_ink_overlap(const Face *f, int h, int m) {
  char buf[220];
  const Element *prev = NULL;
  for (int i = 0; i < f->count; i++) {
    const Element *e = &f->items[i];
    if (e->row == ROW_DATE || e->row == ROW_MINUTES_INLINE) {
      continue;        /* the date is its own line; the inline annotation
                        * shares a baseline rather than stacking */
    }
    if (prev) {
      const int upper_bottom = drawn_ink_bottom(prev);
      const int lower_base = drawn_baseline_of(e);
      const int lower_asc = WORD_INK[e->word].asc;
      const int lower_top = lower_base - lower_asc;
      const int overlap = upper_bottom - lower_top;
      const int limit = (lower_asc * INK_OVERLAP_MAX_PCT) / 100;

      snprintf(buf, sizeof(buf),
               "word %d descends to %d, word %d rises to %d: overlap %d of a "
               "%d ascender (limit %d)", prev->word, upper_bottom, e->word,
               lower_top, overlap, lower_asc, limit);
      CHECK(overlap <= limit, "rows interweave too deeply", h, m, buf);

      /* and neither may reach the other's x-height band */
      const int lower_xtop = lower_base - x_height_of(e->word);
      snprintf(buf, sizeof(buf),
               "word %d descends to %d, the x-height below starts at %d",
               prev->word, upper_bottom, lower_xtop);
      CHECK(upper_bottom < lower_xtop, "a descender reaches the x-height "
            "of the line below", h, m, buf);

      const int upper_base = drawn_baseline_of(prev);
      snprintf(buf, sizeof(buf),
               "word %d rises to %d, the baseline above is %d",
               e->word, lower_top, upper_base);
      CHECK(lower_top > upper_base, "an ascender reaches the baseline of the "
            "line above", h, m, buf);
    }
    prev = e;
  }
}

/*
 * WHERE THE WORDS ARE ACTUALLY DRAWN, offsets included.
 *
 * Everything above this point asserts on e->top, which is the layout as
 * COMPUTED. draw_element() then adds row_offset(), and for a long time
 * nothing tested that step at all - so a bug that lived entirely between the
 * two was invisible. It duly appeared: the two halves of a split minute word
 * shared one row, so nudging "twenty-" clear of the top of the screen moved
 * the number below it as well, while the inline "minute(s)" - pinned to that
 * number's baseline at build time but carrying its own row's offset - stayed
 * behind. Setting offset[ROW_MINUTES_INLINE] = -5 by hand produced 683,874
 * checks
 * and 0 failures.
 *
 * These checks close that gap. They are about the RELATIONSHIPS the offsets
 * are supposed to preserve, not about particular pixel values.
 */
static int drawn_baseline(const Element *e) {
  return e->top + WORDS[e->word].base + row_offset(e->row);
}

/*
 * Every element must move by EXACTLY its own row's offset - no more, which
 * would mean a row is dragging a neighbour with it, and no less, which would
 * mean a slider does not reach what it claims to.
 *
 * Stating it that way rather than "the annotation stays level with the
 * number" matters. An early version only compared those two with both
 * offsets set alike, and the old shared-row behaviour passed it cleanly:
 * when the number and the annotation move together they stay level whether
 * or not anything else is tangled up with them. It has to be phrased as
 * independence, or it does not test the thing that broke.
 *
 * Each row is given a DISTINCT offset, so any two rows wired to the same
 * setting show up immediately rather than cancelling out.
 */
typedef struct { uint8_t row; int off; } OffsetPlan;

static void apply_plan(const OffsetPlan *plan, int n) {
  for (int i = 0; i < n; i++) {
    if (plan[i].row == ROW_SPLIT_HEAD) {
      s_settings.offset_split_head = (int8_t)plan[i].off;
    } else if (plan[i].row == ROW_MINUTES_OWN) {
      s_settings.offset_minutes_own = (int8_t)plan[i].off;
    } else if (plan[i].row == ROW_MINUTE_ALONE) {
      s_settings.offset_minute_alone = (int8_t)plan[i].off;
    } else if (plan[i].row == ROW_MINUTE_SPLIT) {
      s_settings.offset_minute_split = (int8_t)plan[i].off;
    } else if (plan[i].row == ROW_HEDGE) {
      s_settings.offset_hedge = (int8_t)plan[i].off;
    } else if (plan[i].row == ROW_HEDGE_SOLO) {
      s_settings.offset_hedge_solo = (int8_t)plan[i].off;
    } else {
      s_settings.offset[plan[i].row] = (int8_t)plan[i].off;
    }
  }
}

static int plan_for(const OffsetPlan *plan, int n, uint8_t row) {
  for (int i = 0; i < n; i++) {
    if (plan[i].row == row) {
      return plan[i].off;
    }
  }
  return 0;
}

static void check_offset_independence(void) {
  char buf[220];
  /* Distinct values everywhere, except the inline annotation which is held
   * equal to the split number it rides beside - that pairing is the one
   * relationship the separation is supposed to preserve. */
  static const OffsetPlan kPlans[][10] = {
    {{ROW_SPLIT_HEAD, 0}, {ROW_MINUTE, 0}, {ROW_MINUTE_ALONE, 0},
     {ROW_MINUTE_SPLIT, 0}, {ROW_MINUTES_INLINE, 0}, {ROW_MINUTES_OWN, 0},
     {ROW_RELATION, 0}, {ROW_HOUR, 0}, {ROW_HEDGE, 0}, {ROW_HEDGE_SOLO, 0}},
    {{ROW_SPLIT_HEAD, 10}, {ROW_MINUTE, 3}, {ROW_MINUTE_ALONE, -6},
     {ROW_MINUTE_SPLIT, 8}, {ROW_MINUTES_INLINE, 8}, {ROW_MINUTES_OWN, -2},
     {ROW_RELATION, -3}, {ROW_HOUR, -12}, {ROW_HEDGE, 4}, {ROW_HEDGE_SOLO, -7}},
    {{ROW_SPLIT_HEAD, -15}, {ROW_MINUTE, 15}, {ROW_MINUTE_ALONE, 7},
     {ROW_MINUTE_SPLIT, -9}, {ROW_MINUTES_INLINE, -9}, {ROW_MINUTES_OWN, 12},
     {ROW_RELATION, 5}, {ROW_HOUR, -4}, {ROW_HEDGE, -13}, {ROW_HEDGE_SOLO, 10}},
    {{ROW_SPLIT_HEAD, 4}, {ROW_MINUTE, -11}, {ROW_MINUTE_ALONE, 14},
     {ROW_MINUTE_SPLIT, 2}, {ROW_MINUTES_INLINE, 2}, {ROW_MINUTES_OWN, -15},
     {ROW_RELATION, 0}, {ROW_HOUR, 9}, {ROW_HEDGE, 11}, {ROW_HEDGE_SOLO, -2}}
  };
  const int kPlanCount = (int)(sizeof(kPlans) / sizeof(kPlans[0]));
  const int kRows = 10;
  int inline_lines = 0;

  for (int p = 0; p < kPlanCount; p++) {
    for (int mode = 0; mode < MINUTES_MODE_COUNT; mode++) {
     /* The hedge rows only exist in the spoken mode, so the reading mode has
      * to vary here or the two hedge levers are never exercised at all -
      * wiring them to the same field passed everything otherwise. */
     for (int rnd = 0; rnd < ROUNDING_COUNT; rnd++) {
      for (int t = 0; t < 1440; t++) {
        const int h = t / 60, m = t % 60;
        struct tm tm = make_tm(232, 21, 7, 2026, h, m);

        settings_defaults();
        s_settings.minutes_mode = (uint8_t)mode;
        s_settings.rounding = (uint8_t)rnd;
        apply_plan(kPlans[0], kRows);          /* everything at zero */
        s_have_prev = false;
        refresh(&tm, true);
        const Face flat = s_face;

        /* Where the flat pass DRAWS each row, not where it computes it.
         *
         * apply_plan() zeroes the ten levers this sweep owns, but the block
         * lever is not one of them and carries its own non-zero default, so
         * an unoffset flat baseline would charge that default to whichever
         * row happened to be in the phrase. Comparing drawn against drawn
         * cancels every contributor the plan does not touch, which is the
         * property wanted here: this check is about the ten levers moving
         * independently, and check_block_offset() owns the block. */
        int flat_drawn[MAX_ELEMENTS];
        for (int i = 0; i < flat.count; i++) {
          flat_drawn[i] = drawn_baseline(&flat.items[i]);
        }

        settings_defaults();
        s_settings.minutes_mode = (uint8_t)mode;
        s_settings.rounding = (uint8_t)rnd;
        apply_plan(kPlans[p], kRows);
        s_have_prev = false;
        refresh(&tm, true);

        CHECK(s_face.count == flat.count, "offsets changed the layout",
              h, m, "element count");

        for (int i = 0; i < s_face.count && i < flat.count; i++) {
          const Element *e = &s_face.items[i];
          const int want = plan_for(kPlans[p], kRows, e->row);
          const int moved = drawn_baseline(e) - flat_drawn[i];
          snprintf(buf, sizeof(buf),
                   "plan %d mode %d: element %d (word %d, row %d) moved %d, "
                   "its own offset is %d", p, mode, i, e->word, e->row,
                   moved, want);
          CHECK(moved == want,
                "a row did not move by exactly its own offset", h, m, buf);
        }

        /*
         * WHICH LEVER EACH CASE ANSWERS TO.
         *
         * This has to be asserted directly, and it is the part that keeps
         * being learned the hard way. "Every row moves by its own offset"
         * cannot see a word assigned to the wrong row: whichever row it is
         * on, it moves by that row's offset, perfectly consistently. Both
         * times a case was quietly folded back onto a shared lever - the
         * split head, then the number - the whole suite stayed green.
         * Consistency was never the property at risk. Reachability was.
         */
        /*
         * Whether the phrase is stacked at all depends on the SPOKEN minute,
         * not the wall-clock one: with rounding on, :01 speaks as midnight
         * and takes the centred layout, which has no stacked minute row to
         * be on any lever. Testing m != 0 flagged 25 correct faces.
         *
         * And the first element is the hedge when there is one, so the row
         * to inspect is the first non-hedge one.
         */
        int sh = h, sm = m;
        uint8_t sp_hedge;
        round_clock(&sh, &sm, &sp_hedge);
        if (sm != 0) {
          bool split = false, has_own = false;
          int base = 0;
          for (int i = 0; i < s_face.count; i++) {
            if (s_face.items[i].word == W_TWENTYDASH)         split = true;
            if (s_face.items[i].row  == ROW_MINUTES_OWN)      has_own = true;
          }
          if (s_face.count > 0 && s_face.items[0].row == ROW_HEDGE) {
            base = 1;
          }
          if (split) {
            snprintf(buf, sizeof(buf), "head row %d, lower half row %d",
                     s_face.items[base].row, s_face.items[base + 1].row);
            CHECK(s_face.items[base].row == ROW_SPLIT_HEAD,
                  "the split head is not on its own row", h, m, buf);
            CHECK(s_face.items[base + 1].row == ROW_MINUTE_SPLIT,
                  "the split's lower half is not on its own row", h, m, buf);
          } else {
            const uint8_t want = has_own ? ROW_MINUTE : ROW_MINUTE_ALONE;
            snprintf(buf, sizeof(buf),
                     "number is on row %d, expected %d (%s annotation below)",
                     s_face.items[base].row, want, has_own ? "with" : "no");
            CHECK(s_face.items[base].row == want,
                  "the minute number is on the wrong lever", h, m, buf);
          }
        }

        /* An inline "minute(s)" holds the line of the split number it sits
         * beside, given the two are set alike. */
        const Element *host = NULL, *ann = NULL;
        for (int i = 0; i < s_face.count; i++) {
          if (s_face.items[i].row == ROW_MINUTE_SPLIT)  host = &s_face.items[i];
          if (s_face.items[i].row == ROW_MINUTES_INLINE) ann = &s_face.items[i];
        }
        if (host && ann) {
          inline_lines++;
          snprintf(buf, sizeof(buf),
                   "plan %d: annotation baseline %d, number %d",
                   p, drawn_baseline(ann), drawn_baseline(host));
          CHECK(drawn_baseline(ann) == drawn_baseline(host),
                "\"minute(s)\" left the number's line once drawn", h, m, buf);
        }
      }
     }
    }
  }

  settings_defaults();
  s_have_prev = false;
  printf("  drawn offsets        %d inline lines over %d plans x %d modes\n",
         inline_lines, kPlanCount, MINUTES_MODE_COUNT);
}

/*
 * Does a setting sent from the phone reach the field it names?
 *
 * Everything else here starts from s_settings already populated, which
 * leaves the entire AppMessage path untested: a key could be read into the
 * wrong field, or dropped on the floor, and every geometry assertion would
 * still pass because the tests set the values directly. Deleting the write
 * for one offset produced 1,186,049 checks and 0 failures.
 *
 * So this drives the real inbox_received() through a real dictionary, one
 * key at a time, and asserts that the named field moved and no other did.
 */
static void send_setting(uint32_t key, int32_t value) {
  const StubTupleSpec spec = {key, TUPLE_INT, value, NULL};
  stub_set_dict(&spec, 1);
  inbox_received(NULL, NULL);
  stub_set_dict(NULL, 0);
}

static void send_setting_str(uint32_t key, const char *value) {
  const StubTupleSpec spec = {key, TUPLE_CSTRING, 0, value};
  stub_set_dict(&spec, 1);
  inbox_received(NULL, NULL);
  stub_set_dict(NULL, 0);
}

/*
 * Read every offset into one array, in the same order as kOffsets below.
 * Comparing snapshots rather than testing against zero is deliberate: the
 * compile-time defaults are no longer all zero, and an earlier version of
 * this check used "non-zero means it changed", which quietly became a test
 * that every default was zero.
 */
/* One slot per entry in kOffsets[] below. Sized from a name rather than a
 * literal because the two got out of step the moment a lever was added: the
 * table grew, the arrays did not, and the sweep ran off the end of them. */
#define OFFSET_KEY_COUNT 14

static void snapshot_offsets(int *out) {
  out[0] = s_settings.offset_split_head;
  out[1] = s_settings.offset[ROW_MINUTE];
  out[2] = s_settings.offset_minute_alone;
  out[3] = s_settings.offset_minute_split;
  out[4] = s_settings.offset_hedge;
  out[5] = s_settings.offset_hedge_solo;
  out[6] = s_settings.offset_block;
  out[13] = s_settings.offset_block_five;
  out[7] = s_settings.offset[ROW_MINUTES_INLINE];
  out[8] = s_settings.offset_minutes_own;
  out[9] = s_settings.offset[ROW_RELATION];
  out[10] = s_settings.offset[ROW_HOUR];
  out[11] = s_settings.offset[ROW_SOLO];
  out[12] = s_settings.offset[ROW_DATE];
}

static void check_message_routing(void) {
  char buf[180];
  struct { uint32_t key; const char *name; } kOffsets[] = {
    {MESSAGE_KEY_OffSplitHead,   "OffSplitHead"},
    {MESSAGE_KEY_OffMinute,      "OffMinute"},
    {MESSAGE_KEY_OffMinuteAlone, "OffMinuteAlone"},
    {MESSAGE_KEY_OffMinuteSplit, "OffMinuteSplit"},
    {MESSAGE_KEY_OffHedge,       "OffHedge"},
    {MESSAGE_KEY_OffHedgeSolo,   "OffHedgeSolo"},
    {MESSAGE_KEY_OffBlock,       "OffBlock"},
    {MESSAGE_KEY_OffMinutes,     "OffMinutes"},
    {MESSAGE_KEY_OffMinutesOwn,  "OffMinutesOwn"},
    {MESSAGE_KEY_OffRelation,    "OffRelation"},
    {MESSAGE_KEY_OffHour,        "OffHour"},
    {MESSAGE_KEY_OffSolo,        "OffSolo"},
    {MESSAGE_KEY_OffDate,        "OffDate"},
    {MESSAGE_KEY_OffBlockFive,   "OffBlockFive"}
  };
  const int n = (int)(sizeof(kOffsets) / sizeof(kOffsets[0]));
  _Static_assert(sizeof(kOffsets) / sizeof(kOffsets[0]) == OFFSET_KEY_COUNT,
                 "kOffsets[] and snapshot_offsets() disagree on how many "
                 "offset keys there are");

  for (int i = 0; i < n; i++) {
    int before[OFFSET_KEY_COUNT], after[OFFSET_KEY_COUNT];
    settings_defaults();
    snapshot_offsets(before);

    const int target = (before[i] == 7) ? -7 : 7;
    send_setting(kOffsets[i].key, target);
    snapshot_offsets(after);

    int moved = 0, which = -1;
    for (int j = 0; j < n; j++) {
      if (after[j] != before[j]) { moved++; which = j; }
    }
    snprintf(buf, sizeof(buf), "%s moved %d offsets, index %d, value %d",
             kOffsets[i].name, moved, which, after[i]);
    CHECK(moved == 1 && which == i && after[i] == target,
          "an offset key reached the wrong field", 0, 0, buf);
  }

  /* The other settings, and both tuple representations Clay can send. */
  settings_defaults();
  send_setting_str(MESSAGE_KEY_DateFormat, "1");
  CHECK(s_settings.date_format == 1, "DateFormat did not arrive", 0, 0,
        "as a cstring, which is how Clay's select sends it");

  settings_defaults();
  send_setting(MESSAGE_KEY_MinutesText, 2);
  CHECK(s_settings.minutes_mode == 2, "MinutesText did not arrive", 0, 0, "");

  settings_defaults();
  send_setting_str(MESSAGE_KEY_Rounding, "2");
  CHECK(s_settings.rounding == 2, "Rounding did not arrive", 0, 0, "");

  settings_defaults();
  send_setting(MESSAGE_KEY_StrokeWeight, 14);
  CHECK(s_settings.stroke_weight == 14, "StrokeWeight did not arrive", 0, 0, "");

  settings_defaults();
  send_setting(MESSAGE_KEY_ShowDate, 0);
  CHECK(!s_settings.show_date, "ShowDate did not arrive", 0, 0, "");

  /* Out of range must still be clamped on the way in. */
  settings_defaults();
  send_setting(MESSAGE_KEY_OffHour, 999);
  CHECK(s_settings.offset[ROW_HOUR] == OFFSET_MAX, "an offset was not clamped",
        0, 0, "999 should land on OFFSET_MAX");

  settings_defaults();
  drop_all_bitmaps();
  printf("  message routing      %d keys\n", n + 5);
}

/* ------------------------------------------------------------------ */
/* Sweeps                                                              */
/* ------------------------------------------------------------------ */

/* "twenty-" must hold still across every minute it appears in. It used to
 * jump up to 12px as the second half of the split word changed height,
 * re-animating a word that had not changed. */
static int s_twentydash_top = INT16_MIN;

/* Play the reveal out frame by frame.
 *
 * build_face() never touches a bitmap, so without this the cache, the
 * sub-bitmap slicing and tint() would all go untested. Running the real
 * update_proc() under ASAN is what proves prune_cache() actually frees what
 * it drops - the 2014 original leaked a layer per word. */
static void play_reveal(void) {
  s_progress = 0;
  for (;;) {
    update_proc(NULL, NULL);
    if (s_progress >= s_total) {
      break;
    }
    s_progress += (WRITE_SPEED * WRITE_FRAME_MS) / 1000;
    if (s_progress > s_total) {
      s_progress = s_total;
    }
  }
}

static void sweep_minutes(uint8_t fmt, uint8_t mode, uint8_t rounding) {
  int redrawn = 0, moved = 0, elements = 0, peak_bitmaps = 0;

  s_settings.date_format = fmt;
  s_settings.minutes_mode = mode;
  s_settings.rounding = rounding;
  s_have_prev = false;
  s_last_yday = -1;
  drop_all_bitmaps();

  for (int t = 0; t < 1440; t++) {
    const int h = t / 60, m = t % 60;
    struct tm tm = make_tm(232, 21, 7, 2026, h, m);   /* 21 Aug 2026 */

    Face before = s_face;
    const bool had_prev = s_have_prev;
    refresh(&tm, false);

    CHECK(s_face.count > 0, "empty face", h, m, "");
    CHECK(s_face.count <= MAX_ELEMENTS, "element overflow", h, m, "");
    for (int i = 0; i < s_face.count; i++) {
      CHECK(s_face.items[i].word < W_COUNT, "word id out of range", h, m, "");
    }

    check_bounds(&s_face, h, m);
    check_indents(&s_face, h, m);
    check_stacking(&s_face, h, m);
    check_reading_order(&s_face, h, m);
    check_pinned_rows(&s_face, h, m);
    check_date_baseline(&s_face, h, m);
    if (fmt == 0) {
      check_format1_unchanged(&s_face, &tm, h, m);
    } else if (fmt == 1) {
      check_weekday_format(&s_face, &tm, h, m);
    }
    check_minutes_mode(&s_face, mode, h, m);
    check_ink_overlap(&s_face, h, m);
    check_hour_family(&s_face, h, m);
    check_rounding(h, m);
    check_hedge_placement(&s_face, h, m);
    if (m % 7 == 0) {   /* every minute would be 3x the sweep */
      check_block_offset(h, m, fmt, mode, rounding);
    }

    for (int i = 0; i < s_face.count; i++) {
      const Element *e = &s_face.items[i];
      if (e->word == W_TWENTYDASH) {
        if (s_twentydash_top == INT16_MIN) {
          s_twentydash_top = e->top;
        }
        char buf[100];
        snprintf(buf, sizeof(buf), "top=%d, first seen at %d", e->top,
                 s_twentydash_top);
        CHECK(e->top == s_twentydash_top, "\"twenty-\" moved", h, m, buf);
      }
      elements++;
      if (e->animate) {
        redrawn++;
        if (had_prev) {
          for (int j = 0; j < before.count; j++) {
            if (before.items[j].word == e->word) { moved++; break; }
          }
        }
      }
    }

    play_reveal();
    if (stub_live_bitmaps() > peak_bitmaps) {
      peak_bitmaps = stub_live_bitmaps();
    }
  }

  /* Only what is on screen may stay resident. */
  char buf[80];
  snprintf(buf, sizeof(buf), "peak %d, ceiling %d", peak_bitmaps, MAX_ELEMENTS);
  CHECK(peak_bitmaps <= MAX_ELEMENTS, "bitmap cache is not being pruned", 0, 0, buf);

  printf("  minutes swept        1440\n");
  printf("  elements placed      %d\n", elements);
  printf("  redrawn per day      %d  (%d were the same word merely moving)\n",
         redrawn, moved);
  printf("  peak live bitmaps    %d  (ceiling %d)\n", peak_bitmaps, MAX_ELEMENTS);
}

/* Every date of a leap year, so all twelve months, both ordinal edge cases
 * (11th-13th take "th") and the 1/2/3 suffixes are all placed. */
static void sweep_dates(uint8_t fmt) {
  static const int kDays[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int yday = 0;
  bool seen_wday[7] = {false, false, false, false, false, false, false};
  s_settings.date_format = fmt;
  s_settings.rounding = DEFAULT_ROUNDING;
  s_settings.minutes_mode = DEFAULT_MINUTES_MODE;   /* the date line is
                                                       independent of it; pin
                                                       it so this sweep does
                                                       not inherit whatever
                                                       ran last */
  for (int mon = 0; mon < 12; mon++) {
    for (int day = 1; day <= kDays[mon]; day++, yday++) {
      struct tm tm = make_tm(yday, day, mon, 2028, 10, 37);
      s_have_prev = false;
      refresh(&tm, true);
      check_bounds(&s_face, mon + 1, day);
      check_date_baseline(&s_face, mon + 1, day);
      if (fmt == 0) {
        check_format1_unchanged(&s_face, &tm, mon + 1, day);
      } else if (fmt == 1) {
        check_weekday_format(&s_face, &tm, mon + 1, day);
        seen_wday[tm.tm_wday % 7] = true;
      }

      const uint8_t want = (day >= 11 && day <= 13) ? W_TH
                         : (day % 10 == 1) ? W_ST
                         : (day % 10 == 2) ? W_ND
                         : (day % 10 == 3) ? W_RD : W_TH;
      bool found = false;
      for (int i = 0; i < s_face.count; i++) {
        if (s_face.items[i].word == want) { found = true; break; }
      }
      CHECK(found, "wrong ordinal suffix", mon + 1, day, "");
    }
  }

  /*
   * Guards the failure mode this sweep was blind to until tm_wday was filled
   * in: if every swept date came out the same weekday, the weekday assertion
   * above would agree with itself all year and prove nothing.
   */
  if (fmt == 1) {
    for (int d = 0; d < 7; d++) {
      char buf[60];
      snprintf(buf, sizeof(buf), "weekday %d never appeared in a whole year", d);
      CHECK(seen_wday[d], "weekday sweep is vacuous", 0, 0, buf);
    }
  }

  printf("  dates swept          %d\n", yday);
}

/*
 * The settings path, which is where a date format actually arrives from.
 *
 * Clay sends a select as a cstring while every other control on the page
 * sends an int, so tuple_int() has to read both - and an out-of-range index
 * must land on the default rather than off the end of kDateFormats.
 */
static void check_settings(void) {
  struct { TupleType type; int32_t i; const char *s; int32_t want;
           const char *why; } cases[] = {
    {TUPLE_INT,     1, NULL, 1,  "int tuple"},
    {TUPLE_CSTRING, 0, "1",  1,  "cstring tuple, as Clay's select sends it"},
    {TUPLE_CSTRING, 0, "0",  0,  "cstring zero"},
    {TUPLE_CSTRING, 0, " 2", 2,  "leading space"},
    {TUPLE_CSTRING, 0, "-3", -3, "negative"},
    {TUPLE_CSTRING, 0, "",   0,  "empty string"},
    {TUPLE_CSTRING, 0, NULL, 0,  "null string"}
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    Tuple t;
    t.type = cases[i].type;
    if (cases[i].type == TUPLE_CSTRING) {
      t.value->cstring = cases[i].s;
    } else {
      t.value->int32 = cases[i].i;
    }
    char buf[100];
    snprintf(buf, sizeof(buf), "%s -> %d, expected %d", cases[i].why,
             (int)tuple_int(&t), (int)cases[i].want);
    CHECK(tuple_int(&t) == cases[i].want, "tuple parse", 0, 0, buf);
  }

  /* Anything outside kDateFormats falls back rather than indexing off it. */
  CHECK(clamp_date_format(-1) == DEFAULT_DATE_FORMAT, "date format clamp",
        0, 0, "negative");
  CHECK(clamp_date_format(DATE_FORMAT_COUNT) == DEFAULT_DATE_FORMAT,
        "date format clamp", 0, 0, "one past the end");
  CHECK(clamp_date_format(9999) == DEFAULT_DATE_FORMAT, "date format clamp",
        0, 0, "far out of range");
  for (uint8_t f = 0; f < DATE_FORMAT_COUNT; f++) {
    CHECK(clamp_date_format(f) == f, "date format clamp", 0, 0, "in range");
  }

  CHECK(clamp_minutes_mode(-1) == DEFAULT_MINUTES_MODE, "minutes mode clamp",
        0, 0, "negative");
  CHECK(clamp_minutes_mode(MINUTES_MODE_COUNT) == DEFAULT_MINUTES_MODE,
        "minutes mode clamp", 0, 0, "one past the end");
  CHECK(clamp_minutes_mode(9999) == DEFAULT_MINUTES_MODE, "minutes mode clamp",
        0, 0, "far out of range");
  for (uint8_t f = 0; f < MINUTES_MODE_COUNT; f++) {
    CHECK(clamp_minutes_mode(f) == f, "minutes mode clamp", 0, 0, "in range");
  }

  CHECK(clamp_rounding(-1) == DEFAULT_ROUNDING, "rounding clamp", 0, 0, "negative");
  CHECK(clamp_rounding(ROUNDING_COUNT) == DEFAULT_ROUNDING, "rounding clamp",
        0, 0, "one past the end");
  for (uint8_t f = 0; f < ROUNDING_COUNT; f++) {
    CHECK(clamp_rounding(f) == f, "rounding clamp", 0, 0, "in range");
  }

  CHECK(clamp_stroke_weight(-1) == DEFAULT_STROKE_WEIGHT, "stroke weight clamp",
        0, 0, "negative");
  CHECK(clamp_stroke_weight(STROKE_WEIGHT_MAX + 1) == DEFAULT_STROKE_WEIGHT,
        "stroke weight clamp", 0, 0, "one past the end");
  CHECK(clamp_stroke_weight(0) == 0, "stroke weight clamp", 0, 0, "0 is off");
  CHECK(clamp_stroke_weight(STROKE_WEIGHT_MAX) == STROKE_WEIGHT_MAX,
        "stroke weight clamp", 0, 0, "max");
  /* A weight inside the outline's own levels would render the outline as
   * another outline level, which tint() would then rewrite again. */
  for (int v = 1; v <= BOLD_RING_TOP; v++) {
    CHECK(clamp_stroke_weight(v) == 0, "stroke weight clamp", 0, 0,
          "inside the outline levels");
  }

  printf("  settings cases       %d\n",
         (int)(sizeof(cases) / sizeof(cases[0])) + 10 + BOLD_RING_TOP
         + DATE_FORMAT_COUNT + MINUTES_MODE_COUNT);
}

/* The wording rules, spot-checked where they are asymmetric on purpose:
 * "midday" only standing alone, "noon" after it, "twelve" before it. */
/*
 * The three "minutes" modes, spelled out at the examples the setting was
 * asked for. check_minutes_mode() already sweeps the rule over all 1440
 * minutes; these pin the specific phrases a person would read on the watch,
 * so a rule that drifts still has to explain itself against real wording.
 */
static void check_minutes_wording(void) {
  struct { uint8_t mode; int h, m; bool want; const char *phrase; } cases[] = {
    {MINS_AUTO,   1,  1, true,  "one minute past one"},
    {MINS_AUTO,   1,  5, false, "five past one"},
    {MINS_AUTO,   9, 36, true,  "twenty-four minutes to ten"},
    {MINS_AUTO,   1, 25, false, "twenty-five past one"},

    {MINS_NEVER,  1,  1, false, "one past one"},
    {MINS_NEVER,  9, 36, false, "twenty-four to ten"},
    {MINS_NEVER,  1, 15, false, "quarter past one"},
    {MINS_NEVER,  1, 30, false, "half past one"},

    {MINS_ALWAYS, 5,  5, true,  "five minutes past five"},
    {MINS_ALWAYS, 1,  1, true,  "one minute past one"},
    {MINS_ALWAYS, 1, 10, true,  "ten minutes past one"},
    {MINS_ALWAYS, 1, 20, true,  "twenty minutes past one"},
    {MINS_ALWAYS, 1, 25, true,  "twenty-five minutes past one"},
    {MINS_ALWAYS, 9, 36, true,  "twenty-four minutes to ten"},
    {MINS_ALWAYS, 1, 15, false, "quarter past one, NOT quarter minutes past"},
    {MINS_ALWAYS, 1, 45, false, "quarter to two, NOT quarter minutes to"},
    {MINS_ALWAYS, 1, 30, false, "half past one, NOT half minutes past"},
    {MINS_ALWAYS, 1,  0, false, "one o' clock takes no minutes"}
  };

  s_settings.rounding = ROUND_EXACT;   /* these phrases are the exact ones */
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    s_settings.minutes_mode = cases[i].mode;
    struct tm tm = make_tm(232, 21, 7, 2026, cases[i].h, cases[i].m);
    s_have_prev = false;
    refresh(&tm, true);

    bool found = false;
    for (int j = 0; j < s_face.count; j++) {
      if (s_face.items[j].word == W_MINUTE ||
          s_face.items[j].word == W_MINUTES) {
        found = true;
        break;
      }
    }
    CHECK(found == cases[i].want, "minutes wording",
          cases[i].h, cases[i].m, cases[i].phrase);
  }

  s_settings.minutes_mode = DEFAULT_MINUTES_MODE;
  printf("  minutes wording      %d\n",
         (int)(sizeof(cases) / sizeof(cases[0])));
}

static void check_wording(void) {
  /* These cases describe the default mode; :25 dropping "minutes" is true
   * of MINS_AUTO only, and is a deliberate choice of a different mode. */
  s_settings.minutes_mode = MINS_AUTO;
  s_settings.rounding = ROUND_EXACT;

  struct { int h, m; uint8_t want; const char *why; } cases[] = {
    {0,  0,  W_SOLO_MIDNIGHT, "midnight alone"},
    {12, 0,  W_SOLO_MIDDAY,   "midday alone"},
    {3,  0,  W_SOLO_WITCHING, "the witching hour"},
    {12, 10, W_NOON,          "ten past noon"},
    {11, 50, W_TWELVE,        "ten to twelve"},
    {23, 50, W_MIDNIGHT,      "ten to midnight"},
    {0,  10, W_MIDNIGHT,      "ten past midnight"},
    {1,  15, W_QUARTER,       "quarter past one"},
    {1,  30, W_HALF,          "half past one"},
    {1,  45, W_QUARTER,       "quarter to two"},
    {1,  20, W_TWENTY,        "twenty past one (not kOnes[19])"},
    {1,  40, W_TWENTY,        "twenty to two (not kOnes[19])"},
    {1,  23, W_TWENTYDASH,    "twenty-three splits"},
    {1,  25, W_TWENTYDASH,    "twenty-five splits but drops \"minutes\""},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    struct tm tm = make_tm(232, 21, 7, 2026, cases[i].h, cases[i].m);
    s_have_prev = false;
    refresh(&tm, true);
    bool found = false;
    for (int j = 0; j < s_face.count; j++) {
      if (s_face.items[j].word == cases[i].want) { found = true; break; }
    }
    CHECK(found, "wording", cases[i].h, cases[i].m, cases[i].why);
  }
  /* 25 is a multiple of five, so it splits but takes no "minutes". */
  struct tm tm = make_tm(232, 21, 7, 2026, 1, 25);
  s_have_prev = false;
  refresh(&tm, true);
  for (int j = 0; j < s_face.count; j++) {
    CHECK(s_face.items[j].word != W_MINUTES, "wording", 1, 25,
          "\"minutes\" should be dropped at :25");
  }
  printf("  wording cases        %d\n",
         (int)(sizeof(cases) / sizeof(cases[0])) + 1);
}

int main(void) {
  settings_defaults();

  printf("Handwritten As Spoken - layout tests\n");
  printf("  TIME box %dpx/base %d   ROW_GAP %d   REL_TOP %d   DATE_BASELINE %d\n",
         TIME_BOX_H, TIME_BOX_BASE, ROW_GAP, REL_TOP, DATE_BASELINE);

  /*
   * Driven off DATE_FORMAT_COUNT, not off a literal 2: adding a fourth entry
   * to kDateFormats should pull it into the full sweep without anyone having
   * to remember to widen this loop.
   */
  for (uint8_t fmt = 0; fmt < DATE_FORMAT_COUNT; fmt++) {
    for (uint8_t mode = 0; mode < MINUTES_MODE_COUNT; mode++) {
      for (uint8_t rnd = 0; rnd < ROUNDING_COUNT; rnd++) {
        printf("\ndate %d \"%s\"  x  minutes \"%s\"  x  reading \"%s\"\n",
               fmt, kDateFormats[fmt].sample, kMinutesModes[mode],
               kRoundingModes[rnd]);
        sweep_minutes(fmt, mode, rnd);
      }
    }
    sweep_dates(fmt);
  }

  printf("\n");
  settings_defaults();
  check_wording();
  check_minutes_wording();
  check_palette();
  check_offset_independence();
  check_rounded_modes_agree();
  check_hedge_does_not_move_the_hour();
  check_message_routing();
  check_settings();
  emit_reachability();

  drop_all_bitmaps();
  printf("  live bitmaps at exit %d\n", stub_live_bitmaps());
  CHECK(stub_live_bitmaps() == 0, "bitmap leak", 0, 0, "");

  printf("\n%d checks, %d failures\n", s_checks, s_failures);
  if (s_failures) {
    printf("FAILED\n");
    return 1;
  }
  printf("OK\n");
  return 0;
}
