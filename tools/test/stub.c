/* Implementation of the Pebble stub. See pebble.h for why this exists. */

#include "pebble.h"

#include <stdlib.h>

static int s_live;

GBitmap *gbitmap_create_with_resource(uint32_t resource_id) {
  (void)resource_id;
  GBitmap *b = calloc(1, sizeof(GBitmap));
  /* A plausible source palette: tune.py encodes level 0-15 as (level & 3) in
   * red and (level >> 2) in green, and tint() decodes it back. Reproducing it
   * here means tint() is exercised on realistic input rather than zeroes. */
  for (int i = 0; i < 16; i++) {
    b->palette[i] = gcolor_make(3, (uint8_t)(i & 3), (uint8_t)(i >> 2), 0);
  }
  s_live++;
  return b;
}

GBitmap *gbitmap_create_as_sub_bitmap(const GBitmap *base, GRect sub) {
  (void)sub;
  GBitmap *b = calloc(1, sizeof(GBitmap));
  if (base) {
    *b = *base;
  }
  s_live++;
  return b;
}

GColor *gbitmap_get_palette(GBitmap *bmp) { return bmp ? bmp->palette : NULL; }

void gbitmap_destroy(GBitmap *bmp) {
  if (bmp) {
    s_live--;
    free(bmp);
  }
}

int stub_live_bitmaps(void) { return s_live; }

void graphics_context_set_compositing_mode(GContext *ctx, GCompOp mode) {
  (void)ctx; (void)mode;
}
void graphics_draw_bitmap_in_rect(GContext *ctx, const GBitmap *bmp, GRect r) {
  (void)ctx; (void)bmp; (void)r;
}

/* The watchface only ever passes these around, so one instance of each
 * stands in for the whole UI. */
struct Layer { int unused; };
struct Window { int unused; };
struct AppTimer { int unused; };

static Layer s_dummy_layer;
static Window s_dummy_window;

Layer *layer_create(GRect frame) { (void)frame; return &s_dummy_layer; }
void layer_destroy(Layer *layer) { (void)layer; }
void layer_set_update_proc(Layer *l, LayerUpdateProc p) { (void)l; (void)p; }
void layer_add_child(Layer *p, Layer *c) { (void)p; (void)c; }
void layer_mark_dirty(Layer *layer) { (void)layer; }
GRect layer_get_bounds(Layer *layer) { (void)layer; return GRect(0, 0, 200, 228); }

Window *window_create(void) { return &s_dummy_window; }
void window_destroy(Window *w) { (void)w; }
void window_set_window_handlers(Window *w, WindowHandlers h) { (void)w; (void)h; }
void window_stack_push(Window *w, bool a) { (void)w; (void)a; }
void window_set_background_color(Window *w, GColor c) { (void)w; (void)c; }
Layer *window_get_root_layer(Window *w) { (void)w; return &s_dummy_layer; }

static struct AppTimer s_dummy_timer;
AppTimer *app_timer_register(uint32_t ms, AppTimerCallback cb, void *data) {
  (void)ms; (void)cb; (void)data;
  return &s_dummy_timer;
}
void app_timer_cancel(AppTimer *t) { (void)t; }

/* Persistence starts empty, so settings_load() falls through to the
 * compile-time defaults - which is the state a fresh install is in. */
static bool s_persist_valid;
static uint8_t s_persist[256];
static size_t s_persist_len;

int persist_read_data(uint32_t key, void *buf, size_t size) {
  (void)key;
  if (!s_persist_valid) {
    return -1;
  }
  size_t n = size < s_persist_len ? size : s_persist_len;
  memcpy(buf, s_persist, n);
  return (int)n;
}

int persist_write_data(uint32_t key, const void *buf, size_t size) {
  (void)key;
  s_persist_len = size < sizeof(s_persist) ? size : sizeof(s_persist);
  memcpy(s_persist, buf, s_persist_len);
  s_persist_valid = true;
  return (int)s_persist_len;
}

Tuple *dict_find(DictionaryIterator *iter, uint32_t key) {
  (void)iter; (void)key;
  return NULL;
}
void app_message_register_inbox_received(
    void (*cb)(DictionaryIterator *, void *)) { (void)cb; }
void app_message_open(uint32_t in, uint32_t out) { (void)in; (void)out; }

void tick_timer_service_subscribe(TimeUnits u, TickHandler h) { (void)u; (void)h; }
void tick_timer_service_unsubscribe(void) {}
void app_event_loop(void) {}
