/*
 * Handwritten (British)
 *
 * Tells the time the way it is spoken: "quarter past three", "two to six",
 * "twenty-seven minutes to midnight", "midnight".
 *
 * Rewritten from the 2014 bitmap original for the Pebble Time 2 (Emery,
 * 200 x 228). See CONTEXT.md for the layout rules and why they are as they are;
 * every tunable number is in config.h.
 *
 * Words are pre-rendered images, not font text. That is deliberate: drawing
 * text meant guessing where Pebble had put the glyphs in order to reveal them a
 * slice at a time, and the guess was never exact. With images the reveal draws
 * a sub-rectangle, so nothing unrevealed is ever drawn at all.
 */

#include <pebble.h>
#include "config.h"
#include "geometry.h"

#ifndef TIME_BOX_H
#error "src/c/geometry.h predates uniform family boxes. Run: python3 tools/tune.py"
#endif

#ifndef GEOMETRY_HAS_WEEKDAYS
#error "src/c/geometry.h has no weekday words. Run: python3 tools/tune.py"
#endif

/*
 * Backstop for the vertical budget. tools/tune.py performs the full check with
 * real per-word ink extents and refuses to generate if it fails; this catches
 * the case where config.h is edited afterwards and the build is run without
 * re-running tune.py.
 *
 * The tallest phrase is a split minute word - four TIME rows, "twenty-" /
 * "eight" / "past" / "midnight" at :28 - anchored by the relation row at
 * REL_TOP. The hour row's canvas bottom must clear the date's ascenders.
 *
 * The row offsets belong in here: they are applied at draw time, so a budget
 * that ignores them measures a layout nobody sees. Only the two extreme rows
 * bound the phrase, so only their offsets appear.
 */
_Static_assert(REL_TOP + 2 * TIME_BOX_H + ROW_GAP + OFFSET_HOUR
                 < DATE_BASELINE - DATE_BOX_BASE,
               "hour row overlaps the date: lower REL_TOP or ROW_GAP, "
               "or raise DATE_BASELINE");
/* The same row again with each block lever. Asserted separately rather than
 * folded into the line above, because a NEGATIVE block - and both of these
 * default negative or zero - would otherwise make that check too lenient
 * for the exact mode, which no block lever touches. */
_Static_assert(REL_TOP + 2 * TIME_BOX_H + ROW_GAP + OFFSET_HOUR + OFFSET_BLOCK
                 < DATE_BASELINE - DATE_BOX_BASE,
               "the spoken block lever pushes the hour row into the date");
_Static_assert(REL_TOP + 2 * TIME_BOX_H + ROW_GAP + OFFSET_HOUR
                 + OFFSET_BLOCK_FIVE < DATE_BASELINE - DATE_BOX_BASE,
               "the rounded block lever pushes the hour row into the date");
_Static_assert(REL_TOP - 2 * (TIME_BOX_H + ROW_GAP) + OFFSET_SPLIT_HEAD
                 + (TIME_BOX_BASE - SPLIT_HEAD_ASC) >= 0,
               "top row is clipped by the top of the screen: raise REL_TOP, "
               "or lower ROW_GAP or FONT_SIZE_TIME");
_Static_assert(DATE_TOP_LIMIT <= DATE_BASELINE - DATE_BOX_BASE,
               "DATE_TOP_LIMIT is below the date's own ascenders");
_Static_assert(DATE_BASELINE + (DATE_BOX_H - DATE_BOX_BASE) <= SCREEN_H,
               "date descenders fall off the bottom of the screen");

/* ------------------------------------------------------------------ */
/* Rows and elements                                                   */
/* ------------------------------------------------------------------ */

/*
 * The nudge sliders act per ROW, so anything that needs to be nudged
 * independently needs its own row - which is why the two halves of a split
 * minute word are separate rows rather than one.
 *
 * They used to share ROW_MINUTE. Pulling "twenty-" down away from the top of
 * the screen therefore pulled "one" down with it, and the inline "minutes"
 * - which is pinned to "one"'s BASELINE at build time but takes ROW_MINUTES_INLINE'
 * offset at draw time - stayed where it was. The result read as "minutes"
 * floating, when it was the number that had moved.
 */
typedef enum {
  ROW_MINUTE = 0,   /* the minute number, or the SECOND half of a split one */
  ROW_MINUTES_INLINE, /* "minute(s)" riding beside a split number           */
  ROW_RELATION,     /* "past" / "to"                                        */
  ROW_HOUR,         /* the hour word                                        */
  ROW_SOLO,         /* midnight / midday standing alone                     */
  ROW_DATE,

  /*
   * The FIRST half of a split minute word - "twenty-". Added after ROW_DATE
   * rather than next to ROW_MINUTE because the offsets are persisted as a
   * raw array and inserting into the middle would reinterpret every existing
   * watch's saved settings. See Settings.
   */
  ROW_SPLIT_HEAD,

  /*
   * "minute(s)" when it has a LINE TO ITSELF, which is every minute from one
   * to twenty. Separate from the inline case because the two want different
   * things: the inline one has to track the number it sits beside, while
   * this one is a row of its own and its distance from "past"/"to" is a
   * matter of taste.
   */
  ROW_MINUTES_OWN,

  /*
   * The minute number in the two situations that are NOT "number with
   * minute(s) stacked below it". Split out because the three want different
   * placement and share only the word:
   *
   *   ROW_MINUTE_ALONE  nothing sits between it and "past"/"to", so the gap
   *                     beneath is empty and it reads as floating high.
   *   ROW_MINUTE_SPLIT  the lower half of "twenty-one", with or without
   *                     "minute(s)" beside it - its right position does not
   *                     depend on that, so both cases share this row.
   *
   * ROW_MINUTE keeps the stacked case, and with it the o'clock top word and
   * the witching hour's "the", which are centred rather than hung off
   * REL_TOP and have never had a lever of their own.
   */
  ROW_MINUTE_ALONE,
  ROW_MINUTE_SPLIT,

  /*
   * "just gone" / "nearly", above the minute word in the spoken rounding
   * mode. It does NOT take an indent step - it sits at the same x as the
   * word it qualifies, because it modifies that word rather than being a
   * rung of its own in the staircase. At one step in, "twenty-five" would
   * end exactly on the right edge.
   */
  ROW_HEDGE,

  /*
   * The hedge in the CENTRED layouts, separate from the stacked one. Same
   * word, but one sits in a row slot the minute number would otherwise fill
   * and the other hangs off the middle of the screen with the o'clock
   * wording - different balance, different lever.
   */
  ROW_HEDGE_SOLO,
  ROW_COUNT
} RowId;

/*
 * How many offsets the persisted settings blob holds. FROZEN at the six rows
 * that existed when the struct was first written; ROW_SPLIT_HEAD's offset is
 * appended to the end of Settings instead, so old blobs still load.
 */
#define OFFSET_ROWS 6
_Static_assert(ROW_SPLIT_HEAD == OFFSET_ROWS,
               "OFFSET_ROWS must cover exactly the rows stored in the array");

#define MAX_ELEMENTS 16

typedef struct {
  uint8_t word;     /* WordId */
  uint8_t row;      /* RowId  */
  int16_t x;
  int16_t top;      /* canvas top, before the row offset */
  bool animate;
} Element;

typedef struct {
  Element items[MAX_ELEMENTS];
  int count;
} Face;

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

/*
 * APPEND-ONLY. settings_load() reads this straight out of persistent storage
 * as raw bytes, so a watch upgrading from an older build supplies a SHORTER
 * blob: persist_read_data fills what it has and leaves the rest at the
 * defaults set moments earlier. That works only while new fields go on the
 * END - inserting one in the middle would reinterpret every existing user's
 * colours and row offsets as something else.
 */
typedef struct {
  GColor paper;
  GColor ink;
  bool show_date;
  int8_t offset[OFFSET_ROWS];   /* frozen size - see OFFSET_ROWS */
  uint8_t date_format;          /* index into kDateFormats  */
  uint8_t minutes_mode;         /* index into kMinutesModes */
  uint8_t stroke_weight;        /* outline level, dark ink only */
  int8_t offset_split_head;     /* ROW_SPLIT_HEAD, appended not inserted */
  int8_t offset_minutes_own;    /* ROW_MINUTES_OWN, likewise             */
  int8_t offset_minute_alone;   /* ROW_MINUTE_ALONE                      */
  int8_t offset_minute_split;   /* ROW_MINUTE_SPLIT                      */
  int8_t offset_hedge;          /* ROW_HEDGE                             */
  uint8_t rounding;             /* index into kRoundingModes             */
  int8_t offset_hedge_solo;     /* ROW_HEDGE_SOLO                        */
  int8_t offset_block;          /* the whole phrase, as one - SPOKEN     */
  int8_t offset_block_five;     /* the same, for the plain rounded mode  */
} Settings;

#define SETTINGS_KEY 2

static Settings s_settings;
static Window *s_window;
static Layer *s_layer;

static Face s_face, s_prev;
static bool s_have_prev;

static GBitmap *s_cache[W_COUNT];   /* only the words on screen are loaded */

static AppTimer *s_timer;
static int s_progress, s_total;
static int s_last_yday = -1;

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

static const uint8_t kOnes[19] = {
  W_ONE, W_TWO, W_THREE, W_FOUR, W_FIVE, W_SIX, W_SEVEN, W_EIGHT, W_NINE,
  W_TEN, W_ELEVEN, W_TWELVE, W_THIRTEEN, W_FOURTEEN, W_FIFTEEN, W_SIXTEEN,
  W_SEVENTEEN, W_EIGHTEEN, W_NINETEEN
};

static const uint8_t kMonths[12] = {
  W_MON1, W_MON2, W_MON3, W_MON4, W_MON5, W_MON6,
  W_MON7, W_MON8, W_MON9, W_MON10, W_MON11, W_MON12
};

static const uint8_t kDigits[10] = {
  W_D0, W_D1, W_D2, W_D3, W_D4, W_D5, W_D6, W_D7, W_D8, W_D9
};

/* Indexed by struct tm's tm_wday, so SUNDAY FIRST. tools/tune.py's WEEKDAYS
 * list is in the same order for the same reason; the two must not drift. */
static const uint8_t kWeekdays[7] = {
  W_DOW_SUN, W_DOW_MON, W_DOW_TUE, W_DOW_WED, W_DOW_THU, W_DOW_FRI, W_DOW_SAT
};

/*
 * On the hour the phrase is two or three short words with the whole screen to
 * itself, so it is set in the SOLO family - the size the lone "midnight" has
 * always used. Indexed 1..11; twelve and midnight never reach here, because
 * 00:00 and 12:00 take the solo branch instead.
 */
static const uint8_t kSoloOnes[11] = {
  W_SOLO_ONE, W_SOLO_TWO, W_SOLO_THREE, W_SOLO_FOUR, W_SOLO_FIVE, W_SOLO_SIX,
  W_SOLO_SEVEN, W_SOLO_EIGHT, W_SOLO_NINE, W_SOLO_TEN, W_SOLO_ELEVEN
};

static uint8_t ordinal_word(int day) {
  if (day >= 11 && day <= 13) {
    return W_TH;
  }
  switch (day % 10) {
    case 1:  return W_ST;
    case 2:  return W_ND;
    case 3:  return W_RD;
    default: return W_TH;
  }
}

/*
 * When the "minute(s)" annotation is spoken.
 *
 * Index order is the wire format - the phone stores and sends it - so new
 * modes go on the end and these are never reordered.
 */
typedef enum {
  MINS_AUTO = 0,    /* only when the minute is not a multiple of five */
  MINS_NEVER,       /* never                                          */
  MINS_ALWAYS       /* after numbers, but not after "quarter"/"half"  */
} MinutesMode;

static const char *const kMinutesModes[] = {
  "only when not a multiple of five",
  "never",
  "after numbers, not after quarter/half"
};

#define MINUTES_MODE_COUNT \
  ((uint8_t)(sizeof(kMinutesModes) / sizeof(kMinutesModes[0])))

/*
 * `mins` is the spoken minute count - the number actually said aloud, so 29
 * at both :29 and :31 - not tm_min.
 *
 * MINS_ALWAYS keys off the two cases where the minute is spoken as a WORD
 * rather than a number. "quarter minutes past" and "half minutes past" are
 * not English, and no amount of "always" makes them so; 15 and 30 are the
 * only two, because 45 is spoken as "quarter to" with mins == 15.
 */
static bool wants_minutes(int mins) {
  switch (s_settings.minutes_mode) {
    case MINS_NEVER:
      return false;
    case MINS_ALWAYS:
      return mins != 15 && mins != 30;
    case MINS_AUTO:
    default:
      return (mins % 5) != 0;
  }
}

/*
 * How the minute is read off the dial.
 *
 * Index order is the wire format - the phone stores and sends it - so new
 * modes go on the end and these are never reordered.
 */
typedef enum {
  ROUND_EXACT = 0,   /* "six minutes past one"                        */
  ROUND_FIVE,        /* "five past one"                               */
  ROUND_SPOKEN       /* "just gone five past one" / "nearly ten past" */
} RoundingMode;

static const char *const kRoundingModes[] = {
  "exact",
  "nearest five",
  "nearest five, spoken"
};

#define ROUNDING_COUNT \
  ((uint8_t)(sizeof(kRoundingModes) / sizeof(kRoundingModes[0])))

/*
 * Round the clock to the nearest five minutes, and say which way it went.
 *
 * Everything rounds to the NEAREST tick - no landmark gets a wider pull
 * toward the hour or the half. People do reference times against the
 * roundest mark nearby, but nothing found in the literature pins down how
 * wide that window should be, so the rule stays the one that is easy to
 * predict from the dial.
 *
 * :58 and :59 round into the next hour. The DATE is deliberately left
 * alone - it comes from the real time, not the spoken one - so at 23:58
 * the face reads "nearly midnight" above today's date.
 */
static void round_clock(int *h, int *m, uint8_t *hedge) {
  *hedge = W_COUNT;                    /* W_COUNT means "no hedge" */
  if (s_settings.rounding == ROUND_EXACT) {
    return;
  }

  const int rem = *m % 5;
  int offset;                          /* + means the tick is behind us */
  if (rem <= 2) {
    *m -= rem;
    offset = rem;
  } else {
    *m += 5 - rem;
    offset = -(5 - rem);
  }
  if (*m >= 60) {
    *m = 0;
    *h = (*h + 1) % 24;
  }

  if (s_settings.rounding == ROUND_SPOKEN && offset != 0) {
    *hedge = (offset > 0) ? W_JUST_GONE : W_NEARLY;
  }
}

/*
 * Twelve o'clock is asymmetric on purpose: "midday" only when it stands alone,
 * "noon" after it, "twelve" before it. "midnight" is used on both sides.
 */
static uint8_t hour_word(int hour24, bool past) {
  if (hour24 % 12 == 0) {
    if (hour24 == 0) {
      return W_MIDNIGHT;
    }
    return past ? W_NOON : W_TWELVE;
  }
  return kOnes[hour24 % 12 - 1];
}

/* ------------------------------------------------------------------ */
/* Bitmaps                                                             */
/* ------------------------------------------------------------------ */

/*
 * Images are 4-bit palettised. The colours baked into the PNG are arbitrary -
 * only their DISTINCTNESS matters, because this rewrites the whole palette at
 * load with a ramp from paper to ink. That is what keeps the colour settings
 * working while still allowing antialiased edges.
 *
 * The source palette must not be greyscale. Pebble has two bits per channel, so
 * only four greys exist; sixteen grey levels collapse to four colours during
 * conversion and the SDK re-indexes the pixels to match, which destroys the
 * index-to-intensity mapping and renders every word invisible.
 */
/*
 * Rough relative luminance of a Pebble colour, 0..255. The weights are the
 * usual perceptual ones; only the ORDER of two results is ever used, to ask
 * which of ink and paper is the darker.
 */
static int luma(GColor c) {
  return (54 * c.r + 182 * c.g + 18 * c.b) / 3;
}

static void tint(GBitmap *bmp) {
  GColor *pal = gbitmap_get_palette(bmp);
  if (!pal) {
    return;
  }

  /* Paper and ink as 0-255 per channel. GColor stores two bits each. */
  const int pr = s_settings.paper.r * 85, pg = s_settings.paper.g * 85,
            pb = s_settings.paper.b * 85;
  const int ir = s_settings.ink.r * 85, ig = s_settings.ink.g * 85,
            ib = s_settings.ink.b * 85;

  /*
   * Dark ink on light paper is the case that needs the emboldening outline,
   * because that is the direction the panel renders thin - light strokes
   * bloom, dark ones do not. Decided from luminance rather than from a
   * black/white test so that colour-on-colour schemes get it too.
   */
  const bool dark_ink = luma(s_settings.ink) < luma(s_settings.paper);
  const int ring = dark_ink ? s_settings.stroke_weight : 0;

  for (int i = 0; i < 16; i++) {
    /*
     * Recover the intensity from the colour ALREADY in the palette rather than
     * from the index, because the SDK is free to reorder a palette during
     * conversion. tune.py encodes level 0-15 as (level & 3) in red and
     * (level >> 2) in green, so this decodes it wherever the entry landed.
     */
    int lvl = pal[i].r + 4 * pal[i].g;

    /*
     * Levels 1..BOLD_RING_TOP are the bolder outline, not part of the word.
     * On light ink they render as paper, which is exactly what those two
     * levels did before they carried anything: invisible either way.
     */
    if (lvl > 0 && lvl <= BOLD_RING_TOP) {
      lvl = ring;
    }

    if (lvl <= 0) {
      pal[i] = GColorClear;
      continue;
    }
    /*
     * Blend at full 8-bit precision, then round to the nearest level Pebble
     * can show. Blending in 2-bit space instead loses so much to truncation
     * that only the very brightest pixels come out as ink, and strokes render
     * grey.
     */
    const int r = (pr * (15 - lvl) + ir * lvl) / 15;
    const int g = (pg * (15 - lvl) + ig * lvl) / 15;
    const int b = (pb * (15 - lvl) + ib * lvl) / 15;
    pal[i] = GColorFromRGB(((r + 42) / 85) * 85,
                           ((g + 42) / 85) * 85,
                           ((b + 42) / 85) * 85);
  }
}


static GBitmap *bitmap_for(uint8_t word) {
  if (word >= W_COUNT) {
    return NULL;
  }
  if (!s_cache[word]) {
    s_cache[word] = gbitmap_create_with_resource(WORDS[word].res);
    if (s_cache[word]) {
      tint(s_cache[word]);
    }
  }
  return s_cache[word];
}

/* Drop anything not on screen. Called after every rebuild, so at most the
 * dozen words currently visible are ever resident. */
static void prune_cache(void) {
  for (int i = 0; i < W_COUNT; i++) {
    if (!s_cache[i]) {
      continue;
    }
    bool used = false;
    for (int j = 0; j < s_face.count; j++) {
      if (s_face.items[j].word == i) {
        used = true;
        break;
      }
    }
    if (!used) {
      gbitmap_destroy(s_cache[i]);
      s_cache[i] = NULL;
    }
  }
}

static void drop_all_bitmaps(void) {
  for (int i = 0; i < W_COUNT; i++) {
    if (s_cache[i]) {
      gbitmap_destroy(s_cache[i]);
      s_cache[i] = NULL;
    }
  }
}

/* ------------------------------------------------------------------ */
/* Building the face                                                   */
/* ------------------------------------------------------------------ */

static Element *push(Face *f, uint8_t word, RowId row) {
  if (f->count >= MAX_ELEMENTS) {
    return NULL;
  }
  Element *e = &f->items[f->count++];
  e->word = word;
  e->row = row;
  e->x = 0;
  e->top = 0;
  e->animate = false;
  return e;
}

/*
 * Insert an already-positioned element at `at`, shifting later elements
 * right. The reveal animation plays in ARRAY order, so this exists purely to
 * put an element where it should be READ, not where its geometry happened to
 * be computed. The split-word "minutes" annotation is one such case: its
 * position depends on its neighbour, so it is easiest to compute last, but it
 * must animate third ("twenty-" / "nine" / "minutes" / "to" / "three"), not
 * fifth just because it was appended last.
 */
static void insert_element(Face *f, int at, Element e) {
  if (f->count >= MAX_ELEMENTS) {
    return;
  }
  for (int i = f->count; i > at; i--) {
    f->items[i] = f->items[i - 1];
  }
  f->items[at] = e;
  f->count++;
}

/*
 * Stack canvases downward from `first`, each ROW_GAP below the last. Because
 * canvases are tight around the ink, every visual gap is exactly ROW_GAP.
 */
static void stack(Face *f, int from, int to, int first_top) {
  int y = first_top;
  for (int i = from; i <= to; i++) {
    f->items[i].top = y;
    y += WORDS[f->items[i].word].h + ROW_GAP;
  }
}

static void indent_rows(Face *f, int from, int to) {
  for (int i = from; i <= to; i++) {
    f->items[i].x = MARGIN + (i - from) * INDENT;
  }
}

/* Centre a run of rows vertically in the space above the date. */
static void centre(Face *f, int from, int to) {
  int span = 0;
  for (int i = from; i <= to; i++) {
    span += WORDS[f->items[i].word].h;
  }
  span += ROW_GAP * (to - from);
  stack(f, from, to, (DATE_TOP_LIMIT - span) / 2);
}

/*
 * The date line is built from ATOMS, not from a hard-coded word order.
 *
 * An atom is one readable unit - the weekday, the day-with-its-ordinal, the
 * month, the year - which may expand to several words ("21st" is three). A
 * format is just an ordered list of atoms, and DATE_SPACE goes BETWEEN atoms:
 * never before the first, never after the last, never inside one.
 *
 * That positional rule replaces the old "add a space after the ordinal and
 * after the month" test. The two agree exactly for the original format, but
 * identity-based spacing only worked because the month happened to be
 * followed by the year; in any format ending on the month it would have
 * appended a phantom trailing gap and thrown the centring off. Adding a
 * format should never require touching this function - only kDateFormats.
 */
typedef enum {
  ATOM_END = 0,
  ATOM_WEEKDAY,   /* "Mon."                                   */
  ATOM_DAY,       /* the day number and its raised ordinal    */
  ATOM_MONTH,     /* "Aug."                                   */
  ATOM_YEAR       /* four digits                              */
} DateAtom;

#define DATE_ATOMS_MAX 4    /* atoms in one format, excluding ATOM_END */
#define DATE_MAX_WORDS 10   /* every atom at once: 1 + 3 + 1 + 4, plus slack */

typedef struct {
  const char *sample;                  /* what it looks like; for tests   */
  uint8_t atoms[DATE_ATOMS_MAX + 1];   /* ATOM_END terminated             */
} DateFormatSpec;

/*
 * Order is the wire format: the index is what the phone stores and sends, so
 * NEW FORMATS GO ON THE END. Reordering these would silently change the date
 * for everyone who has already chosen one.
 */
static const DateFormatSpec kDateFormats[] = {
  { "10th Aug. 2026", { ATOM_DAY, ATOM_MONTH, ATOM_YEAR, ATOM_END } },
  { "Mon. 10th Aug.", { ATOM_WEEKDAY, ATOM_DAY, ATOM_MONTH, ATOM_END } }
};

#define DATE_FORMAT_COUNT \
  ((uint8_t)(sizeof(kDateFormats) / sizeof(kDateFormats[0])))

_Static_assert(DATE_MAX_WORDS <= MAX_ELEMENTS,
               "a date format can overflow the element array");

typedef struct {
  uint8_t word[DATE_MAX_WORDS];
  bool raised[DATE_MAX_WORDS];   /* superscript ordinal      */
  bool gap[DATE_MAX_WORDS];      /* DATE_SPACE goes BEFORE it */
  int n;
} DateSeq;

static void date_push(DateSeq *q, uint8_t word, bool raised) {
  if (q->n >= DATE_MAX_WORDS) {
    return;
  }
  q->word[q->n] = word;
  q->raised[q->n] = raised;
  q->gap[q->n] = false;
  q->n++;
}

/* Expand one atom into words. Spacing is not this function's business - the
 * caller marks the gap on whichever word turns out to be the atom's first. */
static void date_atom(DateSeq *q, uint8_t kind, const struct tm *t) {
  switch (kind) {
    case ATOM_WEEKDAY:
      date_push(q, kWeekdays[t->tm_wday % 7], false);
      break;

    case ATOM_DAY: {
      const int day = t->tm_mday;
      if (day >= 10) {
        date_push(q, kDigits[(day / 10) % 10], false);
      }
      date_push(q, kDigits[day % 10], false);
      date_push(q, ordinal_word(day), true);
      break;
    }

    case ATOM_MONTH:
      date_push(q, kMonths[t->tm_mon % 12], false);
      break;

    case ATOM_YEAR: {
      const int year = 1900 + t->tm_year;
      date_push(q, kDigits[(year / 1000) % 10], false);
      date_push(q, kDigits[(year / 100) % 10], false);
      date_push(q, kDigits[(year / 10) % 10], false);
      date_push(q, kDigits[year % 10], false);
      break;
    }

    default:
      break;
  }
}

static void build_date(Face *f, struct tm *t) {
  if (!s_settings.show_date) {
    return;
  }

  /* Persisted settings and phone messages are both untrusted input. */
  const uint8_t fmt = (s_settings.date_format < DATE_FORMAT_COUNT)
                      ? s_settings.date_format : 0;
  const DateFormatSpec *spec = &kDateFormats[fmt];

  DateSeq q;
  q.n = 0;
  for (int i = 0; i < DATE_ATOMS_MAX && spec->atoms[i] != ATOM_END; i++) {
    const int start = q.n;
    date_atom(&q, spec->atoms[i], t);
    if (i > 0 && q.n > start) {
      q.gap[start] = true;   /* the space between this atom and the last */
    }
  }
  if (q.n == 0) {
    return;
  }

  int total = 0;
  for (int i = 0; i < q.n; i++) {
    total += WORDS[q.word[i]].w;
    if (q.gap[i]) {
      total += DATE_SPACE;
    }
  }

  int x = (SCREEN_W - total) / 2;
  for (int i = 0; i < q.n; i++) {
    if (q.gap[i]) {
      x += DATE_SPACE;
    }
    Element *e = push(f, q.word[i], ROW_DATE);
    if (!e) {
      return;
    }
    e->x = x;
    /*
     * Digits have no descenders, so base == height for all of them - which
     * meant lining up the ordinal's canvas TOP with the digit's barely raised
     * it (1-3px, depending on which suffix). What a superscript actually
     * needs is its own BASELINE raised well clear of the digit's, which
     * ORD_RISE does directly. Because base == height here too, this also
     * makes st/nd/rd/th share one consistent raised line instead of each
     * landing at a different height.
     */
    e->top = q.raised[i]
             ? DATE_BASELINE - ORD_RISE - WORDS[q.word[i]].base
             : DATE_BASELINE - WORDS[q.word[i]].base;
    x += WORDS[q.word[i]].w;
  }
}

static void build_face(Face *f, struct tm *t) {
  f->count = 0;
  int h = t->tm_hour;
  int m = t->tm_min;
  uint8_t hedge;
  round_clock(&h, &m, &hedge);
  const bool has_hedge = (hedge != W_COUNT);

  /*
   * The witching hour answers to the REAL clock, not the spoken one. Left on
   * the rounded time it would fire for five minutes either side of three,
   * which both dilutes a once-a-day easter egg and does not fit: three SOLO
   * rows plus a hedge span 203px against the 194 available, and the top row
   * was clipped by 4px. So 02:58 reads "nearly three o' clock" in the
   * ordinary way, and only 03:00 itself is the witching hour.
   */
  const bool witching = (t->tm_hour == WITCHING_HOUR && t->tm_min == 0);

  /*
   * The three CENTRED layouts. All of them are set in the SOLO family and
   * carry ROW_SOLO, so the one offset moves each of them as a block - they
   * hang off the middle of the screen rather than off REL_TOP, and the
   * levers that place the stacked phrase have nothing to say about them.
   */
  const int first = has_hedge ? 1 : 0;   /* index of the first real word */

  if (witching) {
    if (has_hedge) push(f, hedge, ROW_HEDGE_SOLO);
    push(f, W_SOLO_THE, ROW_SOLO);
    push(f, W_SOLO_WITCHING, ROW_SOLO);
    push(f, W_SOLO_HOUR, ROW_SOLO);
    indent_rows(f, first, first + 2);
    if (has_hedge) f->items[0].x = MARGIN;
    centre(f, 0, f->count - 1);
  } else if (m == 0 && (h == 0 || h == 12)) {
    if (has_hedge) push(f, hedge, ROW_HEDGE_SOLO);
    push(f, h == 0 ? W_SOLO_MIDNIGHT : W_SOLO_MIDDAY, ROW_SOLO);
    indent_rows(f, first, first);
    if (has_hedge) f->items[0].x = MARGIN;
    centre(f, 0, f->count - 1);
  } else if (m == 0) {
    if (has_hedge) push(f, hedge, ROW_HEDGE_SOLO);
    push(f, kSoloOnes[h % 12 - 1], ROW_SOLO);
    push(f, W_SOLO_OCLOCK, ROW_SOLO);
    indent_rows(f, first, first + 1);
    if (has_hedge) f->items[0].x = MARGIN;
    centre(f, 0, f->count - 1);
  } else {
    int mins;
    bool past;
    int ref;
    if (m <= 30) {
      mins = m;
      past = true;
      ref = h;
    } else {
      mins = 60 - m;
      past = false;
      ref = (h + 1) % 24;
    }

    /*
     * "twenty-five" fits on one line at 181px - the same 9px right margin
     * "midnight" already lives with on the hour row.
     *
     * Both ROUNDED modes use it. Rounding leaves 25 as the only minute that
     * can land in the 21-29 band at all, so there is nothing left for it to
     * look inconsistent beside, and the spoken mode needs the row back for
     * the hedge. The EXACT mode still splits: there 21-29 all occur, and
     * "twenty-seven" is 206px and would run off the screen.
     */
    const bool one_line_25 =
        (s_settings.rounding != ROUND_EXACT && mins == 25);
    const bool split = (mins >= 21 && mins <= 29) && !one_line_25;
    const bool show_mins = wants_minutes(mins);

    int rel_idx;
    if (has_hedge) {
      push(f, hedge, ROW_HEDGE);
    }
    if (split) {
      /* Two rows, not one: the head is nudged independently of the number
       * it belongs to, so it can be pulled clear of the top of the screen
       * without dragging the rest of the phrase with it. */
      push(f, W_TWENTYDASH, ROW_SPLIT_HEAD);
      push(f, kOnes[mins - 21], ROW_MINUTE_SPLIT);
      rel_idx = first + 2;
    } else {
      uint8_t head;
      if (one_line_25) {
        head = W_TWENTYFIVE;
      } else if (mins == 15) {
        head = W_QUARTER;
      } else if (mins == 20) {
        /* kOnes only covers one..nineteen (19 entries) - kOnes[19] for 20
         * read one past the end. Silent undefined behaviour, not a crash,
         * which is why it quietly drew "one" instead. */
        head = W_TWENTY;
      } else if (mins == 30) {
        head = W_HALF;
      } else {
        head = kOnes[mins - 1];
      }
      /* Which lever this row answers to depends on whether the annotation
       * will sit beneath it - with nothing under it the gap down to
       * "past"/"to" is empty and wants its own placement. */
      push(f, head, show_mins ? ROW_MINUTE : ROW_MINUTE_ALONE);
      rel_idx = first + 1;
      if (show_mins) {
        /* Its own row for non-split words, so it never moves as the number
         * changes and is never redrawn. */
        push(f, mins == 1 ? W_MINUTE : W_MINUTES, ROW_MINUTES_OWN);
        rel_idx = first + 2;
      }
    }

    push(f, past ? W_PAST : W_TO, ROW_RELATION);
    push(f, hour_word(ref, past), ROW_HOUR);
    const int last = f->count - 1;

    /*
     * Fixed indents by ROLE, not by position: relation is always 2 steps in,
     * the hour word always 3, whether or not a middle row (a split word's
     * second half, or "minutes" on its own line) is present. Indenting by
     * position instead meant "past"/"one" sat at indent 1/2 in a 3-row phrase
     * ("quarter past one") but indent 2/3 in a 4-row phrase ("fourteen
     * minutes past one") - so ticking between them moved and redrew both
     * words even though the words themselves had not changed.
     */
    if (has_hedge) {
      f->items[0].x = MARGIN;       /* flat above the word it qualifies */
    }
    f->items[first].x = MARGIN;
    if (rel_idx == first + 2) {
      f->items[first + 1].x = MARGIN + INDENT;
    }
    f->items[rel_idx].x = MARGIN + 2 * INDENT;
    f->items[last].x = MARGIN + 3 * INDENT;

    /* The relation row is pinned; everything else hangs off it. */
    f->items[rel_idx].top = REL_TOP;
    stack(f, rel_idx, last, REL_TOP);
    int y = REL_TOP;
    for (int i = rel_idx - 1; i >= 0; i--) {
      y -= ROW_GAP + WORDS[f->items[i].word].h;
      f->items[i].top = y;
    }

    /*
     * On a split word, "minutes" rides beside the short second half.
     * Inserted at index 2 - right after that word - rather than appended, so
     * it animates in the order it is read: "twenty-" / "nine" / "minutes" /
     * "to" / "three". Appending it left it revealing dead last, after words
     * that come before it in the phrase.
     */
    if (split && show_mins) {
      const Element *host = &f->items[first + 1];
      Element e;
      e.word = mins == 1 ? W_MINUTE : W_MINUTES;
      e.row = ROW_MINUTES_INLINE;
      e.animate = false;
      const int after = host->x + WORDS[host->word].w + MIN_TRAIL;
      const int clamp = SCREEN_W - MARGIN - WORDS[e.word].w;
      e.x = (after < clamp) ? after : clamp;
      /* Baseline-aligned with the word it follows. */
      e.top = host->top + WORDS[host->word].base - WORDS[e.word].base;
      insert_element(f, first + 2, e);
    }
  }

  build_date(f, t);
}

/* ------------------------------------------------------------------ */
/* Change detection                                                    */
/* ------------------------------------------------------------------ */

static bool same(const Element *a, const Element *b) {
  return a->word == b->word && a->row == b->row
      && a->x == b->x && a->top == b->top;
}

static void mark_changes(bool date_changed) {
  s_total = 0;
  for (int i = 0; i < s_face.count; i++) {
    Element *e = &s_face.items[i];
    if (e->row == ROW_DATE) {
      e->animate = date_changed;      /* static all day otherwise */
    } else if (!s_have_prev) {
      e->animate = true;
    } else {
      e->animate = true;
      for (int j = 0; j < s_prev.count; j++) {
        if (same(e, &s_prev.items[j])) {
          e->animate = false;
          break;
        }
      }
    }
    if (e->animate) {
      s_total += WORDS[e->word].w;
    }
  }
  s_progress = 0;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

/*
 * Is this row part of the phrase that hangs off REL_TOP? Those move together
 * when the block lever is used; the hedge, the date and the centred layouts
 * are anchored elsewhere and deliberately stay put.
 */
/*
 * How far the whole phrase is shifted, which depends on the reading mode.
 *
 * The two rounded modes draw the SAME phrase - identical words, identical
 * rows, identical indents - and differ only in that the spoken one carries
 * a hedge above it. So the phrase wants to sit slightly lower there, to
 * leave the hedge its room, and slightly higher in the plain mode, where it
 * is better centred with nothing above it.
 *
 * That is one rigid translation of the block, not a re-layout: the minute,
 * "past"/"to" and the hour hold their spacing exactly. Two levers rather
 * than one so each position can be tuned and then left alone.
 *
 * The exact mode gets neither. Its layout was settled first and must not
 * move when either of these is touched.
 */
static int block_offset(void) {
  switch (s_settings.rounding) {
    case ROUND_SPOKEN: return s_settings.offset_block;
    case ROUND_FIVE:   return s_settings.offset_block_five;
    default:           return 0;
  }
}

static bool in_phrase_block(uint8_t row) {
  switch (row) {
    case ROW_SPLIT_HEAD:
    case ROW_MINUTE:
    case ROW_MINUTE_ALONE:
    case ROW_MINUTE_SPLIT:
    case ROW_MINUTES_OWN:
    case ROW_MINUTES_INLINE:
    case ROW_RELATION:
    case ROW_HOUR:
      return true;
    default:
      return false;
  }
}

static int row_offset(uint8_t row) {
  if (row == ROW_SPLIT_HEAD) {
    return s_settings.offset_split_head + block_offset();
  }
  if (row == ROW_MINUTES_OWN) {
    return s_settings.offset_minutes_own + block_offset();
  }
  if (row == ROW_MINUTE_ALONE) {
    return s_settings.offset_minute_alone + block_offset();
  }
  if (row == ROW_MINUTE_SPLIT) {
    return s_settings.offset_minute_split + block_offset();
  }
  if (row == ROW_HEDGE) {
    return s_settings.offset_hedge;
  }
  if (row == ROW_HEDGE_SOLO) {
    return s_settings.offset_hedge_solo;
  }
  const int own = (row < OFFSET_ROWS) ? s_settings.offset[row] : 0;
  return own + (in_phrase_block(row) ? block_offset() : 0);
}

/*
 * Draw the first `reveal` pixels of a word.
 *
 * Only the revealed slice is drawn - there is no mask over the rest. That is
 * the whole reason this face uses images: with text there was no way to know
 * exactly where the glyphs were, and every masking scheme leaked a few pixels.
 */
static void draw_element(GContext *ctx, const Element *e, int reveal) {
  if (reveal <= 0) {
    return;
  }
  GBitmap *bmp = bitmap_for(e->word);
  if (!bmp) {
    return;
  }
  const WordGeom *g = &WORDS[e->word];
  const int w = (reveal < g->w) ? reveal : g->w;
  const int y = e->top + row_offset(e->row);

  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  if (w >= g->w) {
    graphics_draw_bitmap_in_rect(ctx, bmp, GRect(e->x, y, g->w, g->h));
    return;
  }
  GBitmap *slice = gbitmap_create_as_sub_bitmap(bmp, GRect(0, 0, w, g->h));
  if (slice) {
    graphics_draw_bitmap_in_rect(ctx, slice, GRect(e->x, y, w, g->h));
    gbitmap_destroy(slice);
  }
}

static void update_proc(Layer *layer, GContext *ctx) {
  int travelled = s_progress;
  for (int i = 0; i < s_face.count; i++) {
    const Element *e = &s_face.items[i];
    if (!e->animate) {
      draw_element(ctx, e, WORDS[e->word].w);
      continue;
    }
    draw_element(ctx, e, travelled);
    travelled -= WORDS[e->word].w;
    if (travelled < 0) {
      travelled = 0;
    }
  }
  (void)layer;
}

static void timer_cb(void *data) {
  s_timer = NULL;
  s_progress += (WRITE_SPEED * WRITE_FRAME_MS) / 1000;
  if (s_progress < s_total) {
    s_timer = app_timer_register(WRITE_FRAME_MS, timer_cb, NULL);
  } else {
    s_progress = s_total;
  }
  layer_mark_dirty(s_layer);
  (void)data;
}

static void start_animation(void) {
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
  if (s_total > 0) {
    s_timer = app_timer_register(WRITE_FRAME_MS, timer_cb, NULL);
  }
  layer_mark_dirty(s_layer);
}

/* ------------------------------------------------------------------ */
/* Refresh                                                             */
/* ------------------------------------------------------------------ */

static void refresh(struct tm *t, bool force) {
  const bool date_changed = force || (t->tm_yday != s_last_yday);
  s_last_yday = t->tm_yday;

  build_face(&s_face, t);
  prune_cache();
  mark_changes(date_changed);

  s_prev = s_face;
  s_have_prev = true;
  start_animation();
}

static void refresh_now(bool force) {
  const time_t now = time(NULL);
  refresh(localtime(&now), force);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  refresh(tick_time, false);
  (void)units_changed;
}

/* ------------------------------------------------------------------ */
/* Settings                                                            */
/* ------------------------------------------------------------------ */

static void settings_defaults(void) {
  s_settings.paper = GColorBlack;
  s_settings.ink = GColorWhite;
  s_settings.show_date = true;
  s_settings.date_format = DEFAULT_DATE_FORMAT;
  s_settings.minutes_mode = DEFAULT_MINUTES_MODE;
  s_settings.stroke_weight = DEFAULT_STROKE_WEIGHT;
  s_settings.offset[ROW_MINUTE] = OFFSET_MINUTE;
  s_settings.offset[ROW_MINUTES_INLINE] = OFFSET_MINUTES;
  s_settings.offset_minutes_own = OFFSET_MINUTES_OWN;
  s_settings.offset_minute_alone = OFFSET_MINUTE_ALONE;
  s_settings.offset_minute_split = OFFSET_MINUTE_SPLIT;
  s_settings.offset_hedge = OFFSET_HEDGE;
  s_settings.offset_hedge_solo = OFFSET_HEDGE_SOLO;
  s_settings.offset_block = OFFSET_BLOCK;
  s_settings.offset_block_five = OFFSET_BLOCK_FIVE;
  s_settings.rounding = DEFAULT_ROUNDING;
  s_settings.offset[ROW_RELATION] = OFFSET_RELATION;
  s_settings.offset[ROW_HOUR] = OFFSET_HOUR;
  s_settings.offset[ROW_SOLO] = OFFSET_SOLO;
  s_settings.offset[ROW_DATE] = OFFSET_DATE;
  s_settings.offset_split_head = OFFSET_SPLIT_HEAD;
}

static void settings_load(void) {
  settings_defaults();
  persist_read_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
}

/*
 * Clay's select control sends its value as a STRING, where the colour, toggle
 * and slider controls all send integers. Rather than depend on which control
 * a setting happens to use today - change a select to a radio group and the
 * type changes under you - read either representation.
 *
 * Parsed by hand rather than with atoi() to keep this free of any assumption
 * about which standard headers the Pebble SDK drags in.
 */
static int32_t tuple_int(const Tuple *t) {
  if (t->type != TUPLE_CSTRING) {
    return t->value->int32;
  }
  const char *s = t->value->cstring;
  if (!s) {
    return 0;
  }
  while (*s == ' ') {
    s++;
  }
  bool neg = false;
  if (*s == '-' || *s == '+') {
    neg = (*s == '-');
    s++;
  }
  int32_t v = 0;
  for (; *s >= '0' && *s <= '9'; s++) {
    if (v > 99999999) {
      break;          /* nothing legitimate is this long; stop before it wraps */
    }
    v = v * 10 + (*s - '0');
  }
  return neg ? -v : v;
}

static uint8_t clamp_date_format(int32_t v) {
  return (v >= 0 && v < DATE_FORMAT_COUNT) ? (uint8_t)v : DEFAULT_DATE_FORMAT;
}

static uint8_t clamp_rounding(int32_t v) {
  return (v >= 0 && v < ROUNDING_COUNT) ? (uint8_t)v : DEFAULT_ROUNDING;
}

static uint8_t clamp_minutes_mode(int32_t v) {
  return (v >= 0 && v < MINUTES_MODE_COUNT) ? (uint8_t)v : DEFAULT_MINUTES_MODE;
}

/* Anything above BOLD_RING_TOP is a valid level for the outline to sit at;
 * 0 turns it off. Out of range falls back rather than producing a palette
 * entry for a level that does not exist. */
static uint8_t clamp_stroke_weight(int32_t v) {
  if (v < 0 || v > STROKE_WEIGHT_MAX) {
    return DEFAULT_STROKE_WEIGHT;
  }
  if (v > 0 && v <= BOLD_RING_TOP) {
    return 0;   /* would render the outline as another outline level */
  }
  return (uint8_t)v;
}

static int8_t clamp_offset(int32_t v) {
  if (v < OFFSET_MIN) v = OFFSET_MIN;
  if (v > OFFSET_MAX) v = OFFSET_MAX;
  return (int8_t)v;
}

static void read_offset(DictionaryIterator *it, uint32_t key, RowId row) {
  Tuple *t = dict_find(it, key);
  if (!t) {
    return;
  }
  const int8_t v = clamp_offset(tuple_int(t));
  if (row == ROW_SPLIT_HEAD) {
    s_settings.offset_split_head = v;
  } else if (row == ROW_MINUTES_OWN) {
    s_settings.offset_minutes_own = v;
  } else if (row == ROW_MINUTE_ALONE) {
    s_settings.offset_minute_alone = v;
  } else if (row == ROW_MINUTE_SPLIT) {
    s_settings.offset_minute_split = v;
  } else if (row == ROW_HEDGE) {
    s_settings.offset_hedge = v;
  } else if (row == ROW_HEDGE_SOLO) {
    s_settings.offset_hedge_solo = v;
  } else if (row < OFFSET_ROWS) {
    s_settings.offset[row] = v;
  }
}

static void inbox_received(DictionaryIterator *it, void *context) {
  Tuple *t;
  if ((t = dict_find(it, MESSAGE_KEY_PaperColor))) {
    s_settings.paper = GColorFromHEX(t->value->int32);
  }
  if ((t = dict_find(it, MESSAGE_KEY_InkColor))) {
    s_settings.ink = GColorFromHEX(t->value->int32);
  }
  if ((t = dict_find(it, MESSAGE_KEY_ShowDate))) {
    s_settings.show_date = tuple_int(t) != 0;
  }
  if ((t = dict_find(it, MESSAGE_KEY_DateFormat))) {
    s_settings.date_format = clamp_date_format(tuple_int(t));
  }
  if ((t = dict_find(it, MESSAGE_KEY_MinutesText))) {
    s_settings.minutes_mode = clamp_minutes_mode(tuple_int(t));
  }
  if ((t = dict_find(it, MESSAGE_KEY_Rounding))) {
    s_settings.rounding = clamp_rounding(tuple_int(t));
  }
  if ((t = dict_find(it, MESSAGE_KEY_StrokeWeight))) {
    s_settings.stroke_weight = clamp_stroke_weight(tuple_int(t));
  }
  read_offset(it, MESSAGE_KEY_OffMinute, ROW_MINUTE);
  read_offset(it, MESSAGE_KEY_OffMinutes, ROW_MINUTES_INLINE);
  read_offset(it, MESSAGE_KEY_OffMinutesOwn, ROW_MINUTES_OWN);
  read_offset(it, MESSAGE_KEY_OffMinuteAlone, ROW_MINUTE_ALONE);
  read_offset(it, MESSAGE_KEY_OffMinuteSplit, ROW_MINUTE_SPLIT);
  read_offset(it, MESSAGE_KEY_OffHedge, ROW_HEDGE);
  read_offset(it, MESSAGE_KEY_OffHedgeSolo, ROW_HEDGE_SOLO);
  if ((t = dict_find(it, MESSAGE_KEY_OffBlock))) {
    s_settings.offset_block = clamp_offset(tuple_int(t));
  }
  if ((t = dict_find(it, MESSAGE_KEY_OffBlockFive))) {
    s_settings.offset_block_five = clamp_offset(tuple_int(t));
  }
  read_offset(it, MESSAGE_KEY_OffRelation, ROW_RELATION);
  read_offset(it, MESSAGE_KEY_OffHour, ROW_HOUR);
  read_offset(it, MESSAGE_KEY_OffSolo, ROW_SOLO);
  read_offset(it, MESSAGE_KEY_OffDate, ROW_DATE);
  read_offset(it, MESSAGE_KEY_OffSplitHead, ROW_SPLIT_HEAD);

  persist_write_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
  window_set_background_color(s_window, s_settings.paper);

  /* Colours are baked into each bitmap's palette, so they must be reloaded. */
  drop_all_bitmaps();
  s_have_prev = false;
  refresh_now(true);
  (void)context;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  window_set_background_color(window, s_settings.paper);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, update_proc);
  layer_add_child(root, s_layer);
  refresh_now(true);
}

static void window_unload(Window *window) {
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
  drop_all_bitmaps();
  layer_destroy(s_layer);
  (void)window;
}

static void init(void) {
  settings_load();
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
  app_message_register_inbox_received(inbox_received);
  /*
   * Clay sends every setting in ONE dictionary, so the inbox has to hold
   * the whole page at once: 7 bytes of tuple header each, plus the value.
   * At 21 keys that is around 225 bytes, and 256 left room for only two or
   * three more sliders. Overflowing does not fail loudly - the message is
   * dropped whole and NO setting arrives, which reads as "the new option
   * isn't in the settings page" rather than as a buffer problem.
   */
  app_message_open(512, 64);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
