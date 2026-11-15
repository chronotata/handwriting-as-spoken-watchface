/*
 * Print where the watchface would put every word, for one moment in time.
 *
 *     dump_layout YYYY-MM-DD HH:MM [--rounding N] [--minutes N] [--format N]
 *
 * One line per DRAW, in draw order:
 *
 *     resource_id x y w h
 *
 * Written for tools/mock_sweep.py, which composites the actual word bitmaps
 * at those coordinates and so produces a screenshot without an emulator.
 *
 *
 * WHY THIS RATHER THAN A LAYOUT IN PYTHON
 * ---------------------------------------
 * Because a second implementation of the layout would be worse than no
 * check at all. It would agree with the C on the day it was written and
 * drift silently afterwards, and every disagreement would be ambiguous -
 * is the watchface wrong, or the model of it? A mock-up that is confidently
 * wrong is worse than an emulator screenshot that is obviously wrong.
 *
 * So this includes src/c/handwritten.c and calls the shipping build_face()
 * through the shipping refresh(), exactly as tools/test/harness.c does.
 * There is no model. If the layout changes, this changes with it, because
 * it IS the layout.
 *
 * It then runs the shipping update_proc() and reports the rectangles the
 * stub recorded, rather than reading s_face and working out where each word
 * would go. That distinction is not academic: the first version of this did
 * print e->x and e->top, and was WRONG, because an element's top is not
 * where it is drawn - draw_element() adds row_offset(e->row), which is where
 * every OFFSET_* in config.h and every Clay slider lives. Every mock-up it
 * produced had all the tuned offsets missing, and looked plausible enough
 * that it took changing an offset by 8px and seeing nothing move to notice.
 *
 * Recomputing "top plus the offset" here would have fixed that case and left
 * the same trap: a second copy of draw_element()'s arithmetic, correct until
 * the day the real one changes. Reporting what the draw call actually
 * received cannot drift, and it comes with the reveal and the erase for
 * free.
 *
 * What it does NOT reproduce is the Pebble display pipeline: the SDK's
 * palette conversion, and the reveal animation, which is drawn a slice at a
 * time and is not a placement question at all. Those still need the
 * emulator. See tools/mock_sweep.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "generated.h"

/* handwritten.c owns main(); this needs its own. Same trick as harness.c. */
#define main watchface_main
#include "../../src/c/handwritten.c"
#undef main

/* Sakamoto. tm_wday must be right or every swept date is a Sunday - the same
 * trap tools/test/harness.c documents at make_tm(). */
static int day_of_week(int year, int mon0, int mday) {
  static const int kShift[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int y = year;
  if (mon0 < 2) {
    y -= 1;
  }
  return (y + y / 4 - y / 100 + y / 400 + kShift[mon0] + mday) % 7;
}

static int day_of_year(int year, int mon0, int mday) {
  static const int kLen[12] = {31, 28, 31, 30, 31, 30,
                               31, 31, 30, 31, 30, 31};
  const int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  int yday = mday - 1;
  for (int i = 0; i < mon0; i++) {
    yday += kLen[i] + (i == 1 ? leap : 0);
  }
  return yday;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr,
            "usage: dump_layout YYYY-MM-DD HH:MM "
            "[--rounding N] [--minutes N] [--format N]\n");
    return 2;
  }

  int year, mon, mday, hour, min;
  if (sscanf(argv[1], "%d-%d-%d", &year, &mon, &mday) != 3 ||
      sscanf(argv[2], "%d:%d", &hour, &min) != 2) {
    fprintf(stderr, "cannot read '%s %s'\n", argv[1], argv[2]);
    return 2;
  }

  /* Named rather than positional, so a caller can set one setting without
   * having to state the others - and so an omitted one keeps the
   * watchface's OWN default rather than whatever the caller guessed that
   * default was. */
  settings_defaults();
  for (int i = 3; i + 1 < argc; i += 2) {
    const int v = atoi(argv[i + 1]);
    if      (!strcmp(argv[i], "--rounding")) s_settings.rounding     = (uint8_t)v;
    else if (!strcmp(argv[i], "--minutes"))  s_settings.minutes_mode = (uint8_t)v;
    else if (!strcmp(argv[i], "--format"))   s_settings.date_format  = (uint8_t)v;
    else {
      fprintf(stderr, "unknown option '%s'\n", argv[i]);
      return 2;
    }
  }

  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_year = year - 1900;
  t.tm_mon = mon - 1;
  t.tm_mday = mday;
  t.tm_hour = hour;
  t.tm_min = min;
  t.tm_wday = day_of_week(year, mon - 1, mday);
  t.tm_yday = day_of_year(year, mon - 1, mday);

  /* force = true, so this is a complete face rather than a diff against
   * whatever was notionally on screen before. A mock-up has no before. */
  refresh(&t, true);

  /* The FINISHED frame. refresh() leaves s_progress at 0 with the reveal
   * timer registered, so drawing now would render nothing; winding it to
   * the end is what tools/test/harness.c's play_reveal() does to get the
   * settled picture. */
  stub_reset_draws();
  s_progress = s_total;
  update_proc(NULL, NULL);

  for (int i = 0; i < stub_draw_count(); i++) {
    const StubDraw d = stub_draw_at(i);
    printf("%u %d %d %d %d\n", (unsigned)d.resource_id, d.x, d.y, d.w, d.h);
  }
  return 0;
}
