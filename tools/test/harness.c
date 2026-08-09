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

static struct tm make_tm(int yday, int mday, int mon, int year, int h, int m) {
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_hour = h; t.tm_min = m;
  t.tm_mday = mday; t.tm_mon = mon; t.tm_year = year - 1900; t.tm_yday = yday;
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
  char buf[160];
  int date_ink_top = SCREEN_H;
  for (int i = 0; i < f->count; i++) {
    if (f->items[i].row == ROW_DATE) {
      int t = ink_top_of(&f->items[i]);
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
             ink_top_of(e), ink_bottom_of(e));
    CHECK(ink_top_of(e) >= 0, "ink clipped at top", h, m, buf);
    CHECK(ink_bottom_of(e) <= SCREEN_H, "ink clipped at bottom", h, m, buf);

    if (e->row != ROW_DATE) {
      snprintf(buf, sizeof(buf), "element %d ink bottom %d, date ink top %d",
               i, ink_bottom_of(e), date_ink_top);
      CHECK(ink_bottom_of(e) < date_ink_top, "time row collides with date", h, m, buf);
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
    if (prev && e->row == ROW_MINUTES && baseline_of(e) == baseline_of(prev)) {
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

static void sweep_minutes(void) {
  int redrawn = 0, moved = 0, elements = 0, peak_bitmaps = 0;

  s_have_prev = false;
  s_last_yday = -1;

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
static void sweep_dates(void) {
  static const int kDays[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int yday = 0;
  for (int mon = 0; mon < 12; mon++) {
    for (int day = 1; day <= kDays[mon]; day++, yday++) {
      struct tm tm = make_tm(yday, day, mon, 2028, 10, 37);
      s_have_prev = false;
      refresh(&tm, true);
      check_bounds(&s_face, mon + 1, day);
      check_date_baseline(&s_face, mon + 1, day);

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
  printf("  dates swept          %d\n", yday);
}

/* The wording rules, spot-checked where they are asymmetric on purpose:
 * "midday" only standing alone, "noon" after it, "twelve" before it. */
static void check_wording(void) {
  struct { int h, m; uint8_t want; const char *why; } cases[] = {
    {0,  0,  W_SOLO_MIDNIGHT, "midnight alone"},
    {12, 0,  W_SOLO_MIDDAY,   "midday alone"},
    {3,  0,  W_WITCHING,      "the witching hour"},
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

  sweep_minutes();
  sweep_dates();
  check_wording();

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
