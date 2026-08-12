/*
 * A hand-written stub of the slice of the Pebble SDK that handwritten.c uses.
 *
 * The point is to compile and run the REAL handwritten.c on a PC, so the
 * layout tests exercise the shipping build_face() rather than a model of it.
 * A Python re-implementation was tried during development and was worse than
 * useless: it agreed with itself, not with the watch. The kOnes[19]
 * out-of-bounds read at :20 is the clearest example - a model would have had
 * its own array and its own bug, or neither.
 *
 * Only what handwritten.c actually calls is here. Anything drawing-related is
 * a no-op: the tests are about geometry, and nothing is rasterised.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* ---------------------------------------------------------------- */
/* Colour                                                            */
/* ---------------------------------------------------------------- */

/* Pebble packs a colour into one byte, two bits per channel. */
typedef union {
  uint8_t argb;
  struct {
    uint8_t b : 2;
    uint8_t g : 2;
    uint8_t r : 2;
    uint8_t a : 2;
  };
} GColor;

static inline GColor gcolor_make(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
  GColor c;
  c.argb = (uint8_t)((a << 6) | (r << 4) | (g << 2) | b);
  return c;
}

#define GColorBlack gcolor_make(3, 0, 0, 0)
#define GColorWhite gcolor_make(3, 3, 3, 3)
#define GColorClear gcolor_make(0, 0, 0, 0)

/* Round each 0-255 channel to Pebble's four levels, exactly as the real
 * macro does - tint() depends on this truncation behaviour. */
#define GColorFromRGB(r, g, b) \
  gcolor_make(3, (uint8_t)((r) / 85), (uint8_t)((g) / 85), (uint8_t)((b) / 85))

#define GColorFromHEX(hex)                                    \
  GColorFromRGB((((hex) >> 16) & 0xFF), (((hex) >> 8) & 0xFF), \
                ((hex) & 0xFF))

/* ---------------------------------------------------------------- */
/* Geometry                                                          */
/* ---------------------------------------------------------------- */

typedef struct { int16_t x, y; } GPoint;
typedef struct { int16_t w, h; } GSize;
typedef struct { GPoint origin; GSize size; } GRect;

static inline GRect GRect_(int16_t x, int16_t y, int16_t w, int16_t h) {
  GRect r = {{x, y}, {w, h}};
  return r;
}
#define GRect(x, y, w, h) GRect_((int16_t)(x), (int16_t)(y), \
                                 (int16_t)(w), (int16_t)(h))

/* ---------------------------------------------------------------- */
/* Bitmaps - allocated so the cache logic is exercised for real,     */
/* but never filled with pixels.                                     */
/* ---------------------------------------------------------------- */

typedef struct { GColor palette[16]; } GBitmap;
typedef struct GContext GContext;

GBitmap *gbitmap_create_with_resource(uint32_t resource_id);
GBitmap *gbitmap_create_as_sub_bitmap(const GBitmap *base, GRect sub);
GColor *gbitmap_get_palette(GBitmap *bmp);
void gbitmap_destroy(GBitmap *bmp);

/* How many bitmaps are live right now - the harness asserts this stays
 * bounded, which is what prune_cache() exists to guarantee. */
int stub_live_bitmaps(void);

/* ---------------------------------------------------------------- */
/* Graphics - no-ops                                                 */
/* ---------------------------------------------------------------- */

typedef enum { GCompOpSet, GCompOpAssign } GCompOp;
void graphics_context_set_compositing_mode(GContext *ctx, GCompOp mode);
void graphics_draw_bitmap_in_rect(GContext *ctx, const GBitmap *bmp, GRect r);

/* ---------------------------------------------------------------- */
/* Layers and windows - no-ops                                       */
/* ---------------------------------------------------------------- */

typedef struct Layer Layer;
typedef struct Window Window;
typedef void (*LayerUpdateProc)(Layer *layer, GContext *ctx);
typedef struct {
  void (*load)(Window *window);
  void (*unload)(Window *window);
} WindowHandlers;

Layer *layer_create(GRect frame);
void layer_destroy(Layer *layer);
void layer_set_update_proc(Layer *layer, LayerUpdateProc proc);
void layer_add_child(Layer *parent, Layer *child);
void layer_mark_dirty(Layer *layer);
GRect layer_get_bounds(Layer *layer);

Window *window_create(void);
void window_destroy(Window *window);
void window_set_window_handlers(Window *window, WindowHandlers handlers);
void window_stack_push(Window *window, bool animated);
void window_set_background_color(Window *window, GColor color);
Layer *window_get_root_layer(Window *window);

/* ---------------------------------------------------------------- */
/* Timers - registered but never fired; the harness drives ticks     */
/* directly so the sweep is deterministic.                           */
/* ---------------------------------------------------------------- */

typedef struct AppTimer AppTimer;
typedef void (*AppTimerCallback)(void *data);
AppTimer *app_timer_register(uint32_t ms, AppTimerCallback cb, void *data);
void app_timer_cancel(AppTimer *timer);

/* ---------------------------------------------------------------- */
/* Persistence - backed by a plain buffer                            */
/* ---------------------------------------------------------------- */

int persist_read_data(uint32_t key, void *buf, size_t size);
int persist_write_data(uint32_t key, const void *buf, size_t size);

/* ---------------------------------------------------------------- */
/* AppMessage                                                        */
/* ---------------------------------------------------------------- */

/* Clay sends a select as a cstring and everything else as an int, so the
 * tuple's TYPE is part of what handwritten.c has to cope with. Modelling it
 * here is what lets the tests feed tuple_int() both representations. */
typedef enum {
  TUPLE_BYTE_ARRAY = 0,
  TUPLE_CSTRING = 1,
  TUPLE_UINT = 2,
  TUPLE_INT = 3
} TupleType;

typedef struct {
  TupleType type;
  union { int32_t int32; uint32_t uint32; const char *cstring; } value[1];
} Tuple;
typedef struct DictionaryIterator DictionaryIterator;
Tuple *dict_find(DictionaryIterator *iter, uint32_t key);

/* Lets the tests hand inbox_received() a real dictionary. Without this the
 * whole settings path - which key writes which field - is never executed,
 * and a setting can be silently dropped with every other test still green. */
typedef struct {
  uint32_t key;
  TupleType type;
  int32_t int32;
  const char *cstring;
} StubTupleSpec;
void stub_set_dict(const StubTupleSpec *specs, int count);
void app_message_register_inbox_received(void (*cb)(DictionaryIterator *, void *));
void app_message_open(uint32_t in, uint32_t out);

#define MESSAGE_KEY_PaperColor 1
#define MESSAGE_KEY_InkColor 2
#define MESSAGE_KEY_ShowDate 3
#define MESSAGE_KEY_OffMinute 4
#define MESSAGE_KEY_OffMinutes 5
#define MESSAGE_KEY_OffRelation 6
#define MESSAGE_KEY_OffHour 7
#define MESSAGE_KEY_OffSolo 8
#define MESSAGE_KEY_OffDate 9
#define MESSAGE_KEY_DateFormat 10
#define MESSAGE_KEY_MinutesText 11
#define MESSAGE_KEY_StrokeWeight 12
#define MESSAGE_KEY_OffSplitHead 13
#define MESSAGE_KEY_OffMinutesOwn 14
#define MESSAGE_KEY_OffMinuteAlone 15
#define MESSAGE_KEY_OffMinuteSplit 16
#define MESSAGE_KEY_Rounding 17
#define MESSAGE_KEY_OffHedge 18
#define MESSAGE_KEY_OffHedgeSolo 19
#define MESSAGE_KEY_OffBlock 20

/* ---------------------------------------------------------------- */
/* Tick service and event loop                                       */
/* ---------------------------------------------------------------- */

typedef enum { SECOND_UNIT = 1, MINUTE_UNIT = 2, HOUR_UNIT = 4, DAY_UNIT = 8 }
  TimeUnits;
typedef void (*TickHandler)(struct tm *tick_time, TimeUnits units_changed);
void tick_timer_service_subscribe(TimeUnits units, TickHandler handler);
void tick_timer_service_unsubscribe(void);
void app_event_loop(void);
