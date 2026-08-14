/*
 * Tests for src/pkjs/custom-clay.js - the drag-only slider behaviour on the
 * settings page.
 *
 *     node tools/test/clay-slider.test.js
 *
 * Run automatically by tools/test/run.sh when node is available, and skipped
 * with a note when it is not. Node is not a build dependency of this project
 * and must not become one.
 *
 * This drives the REAL custom-clay.js against a fake DOM, the same way
 * tools/test/harness.c drives the real handwritten.c against a fake Pebble.
 * A fake DOM is a much smaller lie than a fake watch, but the principle is
 * the one this project keeps returning to: test the shipping code, not a
 * description of it.
 *
 * What a fake DOM CANNOT tell you: whether the phone's browser fires the
 * events in the order assumed here, whether a real finger lands where the
 * geometry says, or whether the page still scrolls. Those need the watch.
 */

'use strict';

var listeners = {};
global.window = {};
global.document = {
  addEventListener: function (type, fn, capture) {
    if (!capture) {
      throw new Error('expected capture-phase registration for ' + type);
    }
    (listeners[type] = listeners[type] || []).push(fn);
  }
};

/* A slider is 200px wide and 28px tall, so Clay's square thumb is 28px and
 * travels across 172px - NOT the full 200. That difference is the whole
 * point of the geometry, and the extreme cases below are chosen to sit
 * outside the slop of a naive `fraction * width`. */
function makeSlider(opts) {
  opts = opts || {};
  var attrs = {};
  return {
    tagName: 'INPUT',
    type: 'range',
    min: opts.min !== undefined ? opts.min : '-15',
    max: opts.max !== undefined ? opts.max : '15',
    value: opts.value !== undefined ? opts.value : '0',
    getBoundingClientRect: function () {
      return opts.rect || { left: 0, width: 200, height: 28 };
    },
    setAttribute: function (k, v) { attrs[k] = String(v); },
    getAttribute: function (k) { return k in attrs ? attrs[k] : null; }
  };
}

function dispatch(type, ev) {
  ev.type = type;
  ev.cancelable = true;
  ev.stopped = false;
  ev.prevented = false;
  ev.stopPropagation = function () { ev.stopped = true; };
  ev.preventDefault = function () { ev.prevented = true; };
  (listeners[type] || []).forEach(function (fn) {
    if (!ev.stopped) { fn(ev); }
  });
  return ev;
}

/* The reachability table the C sweep observed. The settings page keeps its
 * own copy - it cannot require() anything, Clay injects it as source - so
 * the two are compared here rather than shared. */
var REACH = require('./reachability.json');

/* A Clay config item: a value, an enabled flag, and change listeners. */
function makeItem(value) {
  return {
    value: value,
    enabled: true,
    handlers: [],
    get: function () { return this.value; },
    set: function (v) {
      this.value = v;
      this.handlers.forEach(function (fn) { fn(); });
    },
    enable: function () { this.enabled = true; },
    disable: function () { this.enabled = false; },
    on: function (evt, fn) {
      if (evt === 'change') { this.handlers.push(fn); }
    }
  };
}

/* Every control the page might grey out, plus the three that decide it and
 * one - PaperColor - that is governed by nothing and must be left alone. */
var items = {
  Rounding: makeItem('0'),
  MinutesText: makeItem('0'),
  ShowDate: makeItem(true),
  PaperColor: makeItem('000000')
};
Object.keys(REACH).forEach(function (cell) {
  REACH[cell].forEach(function (key) {
    if (!items[key]) { items[key] = makeItem(0); }
  });
});

/* Boot it exactly as Clay does: call with clayConfig as `this`, which
 * registers its AFTER_BUILD handlers, then fire them.
 *
 * ALL of them, in order. Keeping only the last one was enough while there
 * was one; the moment a second was added, silently dropping the first would
 * have left the greying untested while every check below still passed. */
var afterBuilds = [];
var fakeClayConfig = {
  EVENTS: { AFTER_BUILD: 'AFTER_BUILD' },
  on: function (evt, fn) { if (evt === 'AFTER_BUILD') { afterBuilds.push(fn); } },
  getItemByMessageKey: function (key) { return items[key] || null; }
};
require('../../src/pkjs/custom-clay.js').call(fakeClayConfig);
var afterBuild = afterBuilds.length
  ? function () { afterBuilds.forEach(function (fn) { fn(); }); }
  : null;

var pass = 0;
var fail = 0;
function check(name, cond, detail) {
  if (cond) {
    pass++;
  } else {
    fail++;
    console.log('  FAIL ' + name + (detail ? ': ' + detail : ''));
  }
}

check('registers an AFTER_BUILD handler', afterBuild !== null);
if (!afterBuild) {
  console.log('\n1 check, 1 failure');
  process.exit(1);
}
afterBuild();

/* A press, then whatever the browser would do to the value. */
function press(el, x, type) {
  if (type === 'touchstart') {
    dispatch('touchstart', { target: el, touches: [{ clientX: x }] });
  } else {
    dispatch('mousedown', { target: el, clientX: x });
  }
}

var s;
var e;

/* ---- value 0 of -15..15: thumb centre 100, grab zone 80..120 ---- */

s = makeSlider();
press(s, 170);
s.value = '12';
e = dispatch('input', { target: s });
check('tap on far track is undone', s.value === '0', 'value became ' + s.value);
check('tap on far track never reaches Clay', e.stopped === true);

s = makeSlider();
press(s, 100);
s.value = '7';
e = dispatch('input', { target: s });
check('drag from thumb centre is allowed', s.value === '7' && !e.stopped);

s = makeSlider();
press(s, 119);
s.value = '3';
e = dispatch('input', { target: s });
check('grab at the edge of the slop is allowed', s.value === '3' && !e.stopped);

s = makeSlider();
press(s, 126);
s.value = '9';
e = dispatch('input', { target: s });
check('grab just outside the slop is rejected', s.value === '0' && e.stopped,
      'value ' + s.value);

/* ---- the extremes, chosen to catch a missing thumb-travel correction ----
 *
 * At max the thumb centre is 186, not 200; at min it is 14, not 0. Each of
 * these presses is on the thumb under the correct geometry and more than a
 * slop away from where `fraction * width` would put it. */

s = makeSlider({ value: '15' });
press(s, 170);
s.value = '11';
e = dispatch('input', { target: s });
check('thumb at max sits at right - thumb/2', s.value === '11' && !e.stopped,
      'a naive fraction*width puts it at 200 and rejects this');

s = makeSlider({ value: '-15' });
press(s, 30);
s.value = '-11';
e = dispatch('input', { target: s });
check('thumb at min sits at left + thumb/2', s.value === '-11' && !e.stopped,
      'a naive fraction*width puts it at 0 and rejects this');

s = makeSlider({ value: '15' });
press(s, 14);
s.value = '-15';
e = dispatch('input', { target: s });
check('tap at the opposite end is rejected', s.value === '15' && e.stopped);

/* ---- touch ---- */

s = makeSlider();
press(s, 175, 'touchstart');
s.value = '14';
e = dispatch('input', { target: s });
check('touch tap on the track is undone', s.value === '0' && e.stopped,
      'value ' + s.value);

s = makeSlider();
press(s, 98, 'touchstart');
s.value = '2';
e = dispatch('input', { target: s });
check('touch drag from the thumb is allowed', s.value === '2' && !e.stopped);

/* Cancelling touchstart would also cancel any page scroll that began on the
 * slider, which is a worse bug than the one being fixed. */
s = makeSlider();
e = dispatch('touchstart', { target: s, touches: [{ clientX: 175 }] });
check('touchstart is never preventDefaulted', e.prevented === false);

s = makeSlider();
e = dispatch('mousedown', { target: s, clientX: 175 });
check('mousedown off the thumb is preventDefaulted', e.prevented === true);

/* ---- other paths stay open ---- */

s = makeSlider();
press(s, 175);              /* leaves it disarmed */
dispatch('keydown', { target: s });
s.value = '1';
e = dispatch('input', { target: s });
check('arrow keys still adjust', s.value === '1' && !e.stopped);

s = makeSlider({ max: '' });
press(s, 170);
s.value = '5';
e = dispatch('input', { target: s });
check('unreadable max fails open', s.value === '5' && !e.stopped);

s = makeSlider({ rect: { left: 0, width: 0, height: 0 } });
press(s, 170);
s.value = '5';
e = dispatch('input', { target: s });
check('zero-width element fails open', s.value === '5' && !e.stopped);

var text = {
  tagName: 'INPUT', type: 'text', value: 'abc',
  setAttribute: function () {},
  getAttribute: function () { return null; }
};
dispatch('mousedown', { target: text, clientX: 5 });
text.value = 'xyz';
e = dispatch('input', { target: text });
check('the number box beside each slider is left alone',
      text.value === 'xyz' && !e.stopped);

/* ------------------------------------------------------------------ */
/* Greying out                                                         */
/* ------------------------------------------------------------------ */

/*
 * THE SETTINGS PAGE MUST AGREE WITH THE LAYOUT ABOUT WHAT IS DEAD.
 *
 * tools/test/harness.c swept every combination of reading mode, "minutes"
 * wording and the date toggle across all 1440 minutes, recorded which rows
 * the face actually draws, and wrote reachability.json. This drives the real
 * settings-page handler across the same combinations and requires the set of
 * ENABLED controls to match exactly.
 *
 * Comparing behaviour rather than reading the table out of the source: the
 * page could hold a perfectly correct table and still wire it up backwards.
 *
 * Both directions matter, and for different reasons. A control left live
 * when it does nothing sends the user off tuning a dead slider. A control
 * greyed out when it works is worse - the setting is simply unreachable, and
 * nothing about the page suggests why.
 */
function cellName(rnd, mode, date) { return rnd + '|' + mode + '|' + date; }

function applyCell(rnd, mode, date) {
  items.Rounding.set(String(rnd));
  items.MinutesText.set(String(mode));
  items.ShowDate.set(date === 1);
}

var governed = {};
Object.keys(REACH).forEach(function (cell) {
  REACH[cell].forEach(function (key) { governed[key] = true; });
});

var cells = Object.keys(REACH);
check('the C sweep wrote every combination', cells.length === 18,
      'got ' + cells.length);

cells.forEach(function (cell) {
  var parts = cell.split('|');
  applyCell(parts[0], parts[1], Number(parts[2]));

  var expected = REACH[cell].slice().sort();
  var actual = Object.keys(governed).filter(function (key) {
    return items[key].enabled;
  }).sort();

  check('reachable controls are live in ' + cell,
        actual.join(',') === expected.join(','),
        'enabled [' + actual.join(', ') + '] but the layout draws [' +
        expected.join(', ') + ']');
});

/* The specific promises behind the two levers, named so a failure says what
 * broke rather than just which cell. */
applyCell(2, 0, 1);
check('the spoken block lever is live in the spoken mode',
      items.OffBlock.enabled);
check('the rounded block lever is dead in the spoken mode',
      !items.OffBlockFive.enabled);
check('the hedge levers are live in the spoken mode',
      items.OffHedge.enabled && items.OffHedgeSolo.enabled);
check('nothing splits in the spoken mode, so the split levers are dead',
      !items.OffSplitHead.enabled && !items.OffMinuteSplit.enabled);

applyCell(1, 0, 1);
check('the rounded block lever is live in the plain rounded mode',
      items.OffBlockFive.enabled);
check('the spoken block lever is dead in the plain rounded mode',
      !items.OffBlock.enabled);
check('there is no hedge in the plain rounded mode',
      !items.OffHedge.enabled && !items.OffHedgeSolo.enabled);
check('nothing splits in the plain rounded mode either',
      !items.OffSplitHead.enabled && !items.OffMinuteSplit.enabled);

applyCell(0, 0, 1);
check('the exact mode still splits, so its levers are live',
      items.OffSplitHead.enabled && items.OffMinuteSplit.enabled);
check('neither block lever touches the exact mode',
      !items.OffBlock.enabled && !items.OffBlockFive.enabled);

applyCell(0, 0, 0);
check('with the date off, its lever and its format are both dead',
      !items.OffDate.enabled && !items.DateFormat.enabled);

/* Not in the table at all: the colours, and the three selects that drive
 * the greying. Disabling the Rounding select would strand the page in
 * whatever mode it happened to be in. */
check('a control the table does not govern is never touched',
      items.PaperColor.enabled && !governed.PaperColor);

/* An unrecognised combination means the table went stale against a mode
 * that has since been added. Everything must stay usable. */
items.Rounding.set('9');
var allLive = Object.keys(governed).every(function (key) {
  return items[key].enabled;
});
check('an unknown mode leaves every control usable', allLive);

console.log('  clay slider          ' + (pass + fail) + ' checks, ' +
            fail + ' failures');
process.exit(fail ? 1 : 0);
