/*
 * config.h - every tunable number in one place.
 *
 * IMPORTANT: after changing any FONT_SIZE_* value, run
 *
 *     python3 tools/tune.py
 *
 * That regenerates the font entries in package.json and rebuilds inktable.h to
 * match. The SDK takes a font's rasterised size from the trailing number in its
 * resource name, so if the two drift apart the build silently keeps the old
 * size and nothing will look right.
 */

#pragma once

/* ---------------------------------------------------------------- */
/* Screen (Emery / Pebble Time 2)                                    */
/* ---------------------------------------------------------------- */

#define SCREEN_W 200
#define SCREEN_H 228

/* ---------------------------------------------------------------- */
/* Font sizes - BUILD TIME ONLY. Re-run tools/tune.py after editing. */
/* Maximum recommended size is 48.                                   */
/* ---------------------------------------------------------------- */

#define FONT_SIZE_TIME 40  /* minute number, past/to, hour word */
#define FONT_SIZE_SOLO 46  /* midnight / midday standing alone  */
#define FONT_SIZE_MINS 25  /* the small "minute(s)" annotation  */
#define FONT_SIZE_DATE 24  /* day number and year               */
#define FONT_SIZE_ORD  15  /* the superscript st/nd/rd/th       */

/* ---------------------------------------------------------------- */
/* Vertical offsets - compile-time defaults.                         */
/*                                                                   */
/* These are also live sliders on the phone settings page, so you can */
/* nudge rows on the watch without rebuilding. Anything set there     */
/* overrides the values here.                                        */
/*                                                                   */
/* SIGN: standard screen coordinates. NEGATIVE moves a row UP,        */
/* POSITIVE moves it DOWN. Offset +1 on the hour row puts every hour  */
/* word one pixel lower.                                             */
/*                                                                   */
/* Offsets are applied at draw time only. They never feed back into   */
/* the gap or anchor calculations, so nudging one row cannot move     */
/* another - but a large offset can push a row into its neighbour,    */
/* because nothing re-flows to compensate.                            */
/* ---------------------------------------------------------------- */

#define OFFSET_MINUTE   0  /* row 0   - the minute number       */
#define OFFSET_MINUTES  0  /* row 1.5 - "minute" / "minutes"    */
#define OFFSET_RELATION 0  /* row 1   - "past" / "to"           */
#define OFFSET_HOUR     0  /* row 2   - the hour word           */
#define OFFSET_SOLO     0  /* solo midnight / midday            */
#define OFFSET_DATE     0  /* the date line                     */

#define OFFSET_MIN -15
#define OFFSET_MAX  15

/* ---------------------------------------------------------------- */
/* Layout                                                            */
/* ---------------------------------------------------------------- */

#define MARGIN 10       /* left and right screen margin              */
#define INDENT 9        /* staircase step per row                    */

#define ROW_GAP 4       /* gap between stacked CANVASES.                      */
                        /*                                                    */
                        /* Canvas heights are uniform per size family, so the  */
                        /* row pitch is constant (TIME_BOX_H + ROW_GAP) and    */
                        /* baselines land on the same lines all day. That is   */
                        /* what stops a word's neighbours moving when it is    */
                        /* replaced by a different word.                       */
                        /*                                                    */
                        /* This is therefore the MINIMUM visible ink gap, not  */
                        /* the actual one: it is what you see when a full      */
                        /* descender sits directly above a full ascender       */
                        /* ("eight o' clock"). Everywhere else the gap opens   */
                        /* up by whatever ink the two words do not use - up to */
                        /* ROW_GAP + 31 for "to" above "one".                  */
                        /*                                                    */
                        /* Raising this pushes the worst-case phrase into the  */
                        /* date; tools/tune.py checks and refuses to generate. */
#define MIN_TRAIL 6     /* horizontal gap before an inline "minutes" */

/*
 * Fixed canvas top for the relation row ("past" / "to").
 *
 * Pinning this is what stops the relation and hour rows drifting as the minute
 * ticks - they hold position for a whole half hour and are never redrawn. Only
 * the rows above float, upward into the space reserved for the tallest case
 * ("twenty-" / "eight" / "past" / "midnight" at :28).
 */
#define REL_TOP 96

#define DATE_SPACE 7        /* gap before the month, and before the year   */
#define DATE_TOP_LIMIT 194  /* time rows must stay above this (the date's  */
                            /* ascenders reach up to here). Keep this      */
                            /* 2px above DATE_BASELINE - DATE_BOX_BASE.    */

/*
 * Which entry of kDateFormats (handwritten.c) a fresh install starts on, and
 * what an out-of-range value from the phone falls back to.
 *
 *   0  "10th Aug. 2026"
 *   1  "Mon. 10th Aug."
 *
 * Leave this at 0. Existing watches persist their own choice and never read
 * it; changing it only moves people who have never opened the settings page.
 */
#define DEFAULT_DATE_FORMAT 0

#define ORD_RISE 6           /* how far the ordinal's baseline sits above */
                             /* the day/year baseline                     */
#define DATE_BASELINE 215   /* fixed for every date, so months without    */
                            /* descenders do not sit lower than those with */

/* ---------------------------------------------------------------- */
/* Stroke weight for dark ink on a light background                  */
/* ---------------------------------------------------------------- */

/*
 * Levels 1..BOLD_RING_TOP of every word image hold a slightly bolder outline
 * of that word - see the BOLD_BLEND note in tools/tune.py. Rendering it as
 * paper gives the normal weight, rendering it as ink gives the bold one.
 *
 * Photographs of the real watch put black-on-white strokes 19.7% thinner
 * than white-on-black ones, because light strokes bloom on this panel and
 * dark ones do not. The outline is worth about +15.8%, which was chosen to
 * match the NOMINAL width baked into the bitmaps rather than to match
 * white-on-black - that scheme is itself running ~6% fat.
 *
 * DEFAULT_STROKE_WEIGHT is the LEVEL the outline is rendered at when the ink
 * is darker than the paper, so it lands on the same paper-to-ink ramp as
 * everything else. For black on white the four levels per channel collapse
 * sixteen weights into four visibly different ones:
 *
 *      0-2     off - the outline stays invisible, as it does on dark paper
 *      3-7     a third of the way to ink        ("Light" on the phone)
 *      8-12    two thirds of the way            ("Medium", the default)
 *      13-15   solid ink, hardest edge          ("Solid")
 *
 * The phone offers exactly those four, at 0 / 5 / 10 / 14. It used to be a
 * fifteen-step slider, twelve steps of which changed nothing on screen.
 *
 * Note that the weight changes how solidly the outline is INKED, not how far
 * it extends: the outer edge of the outline sets the apparent stroke width
 * either way. Making the strokes genuinely thicker means raising BOLD_BLEND
 * in tools/tune.py and regenerating.
 */
#define BOLD_RING_TOP 2
#define DEFAULT_STROKE_WEIGHT 10
#define STROKE_WEIGHT_MAX 15

/* ---------------------------------------------------------------- */
/* Animation                                                         */
/* ---------------------------------------------------------------- */

#define WRITE_SPEED 260   /* reveal speed, pixels per second */
#define WRITE_FRAME_MS 33 /* redraw interval                 */

/*
 * The reveal mask covers each row's whole vertical band, bounded by the ink of
 * the rows above and below, rather than a rectangle fitted to the glyphs. That
 * makes it independent of font metrics, which is deliberate: fitting the mask
 * to the text repeatedly let a few pixels of tall letters and descenders show
 * through, because neither Pebble's reported text box nor the generated ink
 * table describes a script face's true extents exactly.
 *
 * There is nothing to tune here as a result. If fragments ever appear again,
 * the cause is the LAYOUT, not the mask - most likely OPTICAL_GAP being small
 * enough that two rows' ink nearly touches.
 */

/* ---------------------------------------------------------------- */
/* Wording                                                           */
/* ---------------------------------------------------------------- */

#define MIDDAY_WORD "midday"
#define WITCHING_HOUR 3   /* easter egg fires at this hour, on the hour */

/*
 * Which entry of kMinutesModes (handwritten.c) a fresh install starts on,
 * and what an out-of-range value from the phone falls back to.
 *
 *   0  "twenty-nine minutes to ten", "five past five"   (only when the
 *      minute is not a multiple of five - how v1 always behaved)
 *   1  "twenty-nine to ten", "five past five"           (never)
 *   2  "twenty-nine minutes to ten", "five minutes past five", but still
 *      "quarter past" and "half past"                   (after numbers)
 *
 * Leave this at 0: it is the behaviour every existing watch already has,
 * and watches that have chosen otherwise persist their own value.
 */
#define DEFAULT_MINUTES_MODE 0
