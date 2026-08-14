/**
 * Runs INSIDE the settings page, not in the pkjs sandbox.
 *
 * Clay serialises this function with `.toString()` and injects the source
 * straight into the config page, so it must be entirely self-contained:
 * no closures over module scope, no require(), and ES5 only - it runs in
 * whatever browser engine the Pebble phone app happens to use.
 *
 *
 * WHAT IT FIXES
 * -------------
 * Clay renders each slider as a native `<input type="range">`, and a native
 * range jumps its thumb to wherever you tap the track. Scrolling the
 * settings page with a thumb that brushes a slider therefore nudged a row
 * offset by several pixels, silently, with no way to tell it had happened
 * short of noticing the watch had moved.
 *
 * A row offset is a deliberate act of tuning, so it should take a deliberate
 * gesture: grab the dot and drag it. Tapping the track now does nothing.
 *
 *
 * HOW
 * ---
 * On pointer-down we work out whether the press landed on the thumb, and
 * remember the value at that moment. If it did not, every `input`/`change`
 * the browser then fires is caught in the CAPTURE phase on `document` -
 * before it can reach Clay's own listeners on the element - the value is put
 * back, and propagation stops. Clay never learns anything happened.
 *
 * Restoring the DOM value is enough to be authoritative: Clay's slider
 * manipulator reads `parseFloat(element.value)` live when the page is
 * serialised, so there is no separate internal state to keep in step.
 *
 * The geometry check FAILS OPEN. If the element's metrics cannot be read the
 * press counts as on-thumb, because a slider that behaves as it always did
 * is a far better failure than one that cannot be moved at all.
 *
 * Keyboard adjustment and the number box beside each slider are untouched -
 * both are already deliberate, and the number box is the precise-entry path
 * if dragging on a small screen is fiddly.
 */
module.exports = function() {
  var clayConfig = this;

  /*
   * GREY OUT ANYTHING THAT CANNOT AFFECT WHAT IS ON THE WATCH.
   *
   * Which rows get drawn depends on the wording and date settings: rounding
   * to the nearest five leaves 25 as the only minute in the twenties, so
   * nothing splits across two lines and the split levers are dead; the hedge
   * exists only in the spoken mode; with the date switched off both the date
   * lever and the date format are for something nobody can see.
   *
   * A control that does nothing is worse than a missing one, because the
   * reasonable conclusion on moving it and seeing no change is that the
   * watchface is broken. So they are greyed rather than left live.
   *
   * REACHABLE below is not hand-reasoned. tools/test/harness.c sweeps every
   * combination of these settings across all 1440 minutes, records which
   * rows the layout actually draws, and writes tools/test/reachability.json;
   * tools/test/clay-slider.test.js then drives this very handler across the
   * same combinations and fails if it greys out anything different. Copy the
   * emitted table in here when the layout changes - do not reason it out,
   * the sweep has already done that and will disagree if you get it wrong.
   *
   * Keys are "rounding|minutesText|showDate". Any control not named in a
   * cell is disabled there; anything not named ANYWHERE - the colours, the
   * mode selects themselves - is always live and never touched.
   */
  var REACHABLE = {
    '0|0|0': ['OffMinute', 'OffMinutes', 'OffRelation', 'OffHour', 'OffSolo', 'OffSplitHead', 'OffMinutesOwn', 'OffMinuteAlone', 'OffMinuteSplit'],
    '0|0|1': ['OffMinute', 'OffMinutes', 'OffRelation', 'OffHour', 'OffSolo', 'OffDate', 'OffSplitHead', 'OffMinutesOwn', 'OffMinuteAlone', 'OffMinuteSplit', 'DateFormat'],
    '0|1|0': ['OffRelation', 'OffHour', 'OffSolo', 'OffSplitHead', 'OffMinuteAlone', 'OffMinuteSplit'],
    '0|1|1': ['OffRelation', 'OffHour', 'OffSolo', 'OffDate', 'OffSplitHead', 'OffMinuteAlone', 'OffMinuteSplit', 'DateFormat'],
    '0|2|0': ['OffMinute', 'OffMinutes', 'OffRelation', 'OffHour', 'OffSolo', 'OffSplitHead', 'OffMinutesOwn', 'OffMinuteAlone', 'OffMinuteSplit'],
    '0|2|1': ['OffMinute', 'OffMinutes', 'OffRelation', 'OffHour', 'OffSolo', 'OffDate', 'OffSplitHead', 'OffMinutesOwn', 'OffMinuteAlone', 'OffMinuteSplit', 'DateFormat'],
    '1|0|0': ['OffRelation', 'OffHour', 'OffSolo', 'OffMinuteAlone', 'OffBlockFive'],
    '1|0|1': ['OffRelation', 'OffHour', 'OffSolo', 'OffDate', 'OffMinuteAlone', 'DateFormat', 'OffBlockFive'],
    '1|1|0': ['OffRelation', 'OffHour', 'OffSolo', 'OffMinuteAlone', 'OffBlockFive'],
    '1|1|1': ['OffRelation', 'OffHour', 'OffSolo', 'OffDate', 'OffMinuteAlone', 'DateFormat', 'OffBlockFive'],
    '1|2|0': ['OffMinute', 'OffRelation', 'OffHour', 'OffSolo', 'OffMinutesOwn', 'OffMinuteAlone', 'OffBlockFive'],
    '1|2|1': ['OffMinute', 'OffRelation', 'OffHour', 'OffSolo', 'OffDate', 'OffMinutesOwn', 'OffMinuteAlone', 'DateFormat', 'OffBlockFive'],
    '2|0|0': ['OffRelation', 'OffHour', 'OffSolo', 'OffMinuteAlone', 'OffHedge', 'OffHedgeSolo', 'OffBlock'],
    '2|0|1': ['OffRelation', 'OffHour', 'OffSolo', 'OffDate', 'OffMinuteAlone', 'OffHedge', 'OffHedgeSolo', 'DateFormat', 'OffBlock'],
    '2|1|0': ['OffRelation', 'OffHour', 'OffSolo', 'OffMinuteAlone', 'OffHedge', 'OffHedgeSolo', 'OffBlock'],
    '2|1|1': ['OffRelation', 'OffHour', 'OffSolo', 'OffDate', 'OffMinuteAlone', 'OffHedge', 'OffHedgeSolo', 'DateFormat', 'OffBlock'],
    '2|2|0': ['OffMinute', 'OffRelation', 'OffHour', 'OffSolo', 'OffMinutesOwn', 'OffMinuteAlone', 'OffHedge', 'OffHedgeSolo', 'OffBlock'],
    '2|2|1': ['OffMinute', 'OffRelation', 'OffHour', 'OffSolo', 'OffDate', 'OffMinutesOwn', 'OffMinuteAlone', 'OffHedge', 'OffHedgeSolo', 'DateFormat', 'OffBlock']
  };

  /* Every control the table governs, gathered from the table itself so a
   * key added to one cell cannot be forgotten in another - it will simply
   * be absent there, which is what "disabled" means. */
  var GOVERNED = (function () {
    var seen = {};
    var all = [];
    Object.keys(REACHABLE).forEach(function (cell) {
      REACHABLE[cell].forEach(function (key) {
        if (!seen[key]) { seen[key] = true; all.push(key); }
      });
    });
    return all;
  })();

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    var rounding = clayConfig.getItemByMessageKey('Rounding');
    var minutes = clayConfig.getItemByMessageKey('MinutesText');
    var showDate = clayConfig.getItemByMessageKey('ShowDate');
    if (!rounding || !minutes || !showDate) { return; }

    function sync() {
      /* Clay hands a select's value back as a string and a toggle as a
       * boolean, so both are normalised rather than trusted. */
      var cell = String(rounding.get()) + '|' + String(minutes.get()) + '|' +
                 (showDate.get() ? '1' : '0');
      var live = REACHABLE[cell];
      /* An unknown cell means a mode was added without the table being
       * regenerated. Leave everything enabled: a stale grey-out hides a
       * working control, which is the worse of the two failures. */
      if (!live) { live = GOVERNED; }

      GOVERNED.forEach(function (key) {
        var item = clayConfig.getItemByMessageKey(key);
        if (!item) { return; }
        if (live.indexOf(key) >= 0) { item.enable(); } else { item.disable(); }
      });
    }

    rounding.on('change', sync);
    minutes.on('change', sync);
    showDate.on('change', sync);
    sync();
  });

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    /* AFTER_BUILD can fire again if the page is rebuilt; the listeners are
     * delegated to `document` and only ever need attaching once. */
    if (window.__handwrittenDragOnlySliders) {
      return;
    }
    window.__handwrittenDragOnlySliders = true;

    /* Forgiveness either side of the thumb, in CSS px. Fingers are not
     * precise; without this the control feels broken rather than strict. */
    var SLOP = 6;

    function isSlider(el) {
      return !!el && el.tagName === 'INPUT' && el.type === 'range';
    }

    function pointOf(e) {
      if (e.touches && e.touches.length) {
        return e.touches[0].clientX;
      }
      if (e.changedTouches && e.changedTouches.length) {
        return e.changedTouches[0].clientX;
      }
      return e.clientX;
    }

    /* Where the thumb is now, and did the press land on it?
     *
     * A range thumb travels between `thumb/2` and `width - thumb/2`, not
     * across the full track, so the centre is not simply width * fraction.
     * Clay styles the thumb as a square of the input's own height, which is
     * why rect.height stands in for its width. */
    function onThumb(el, clientX) {
      if (typeof clientX !== 'number') {
        return true;
      }
      var rect = el.getBoundingClientRect();
      var min = parseFloat(el.min);
      var max = parseFloat(el.max);
      var val = parseFloat(el.value);

      if (!isFinite(min)) {
        min = 0;
      }
      if (!isFinite(max) || max <= min || !isFinite(val) || rect.width <= 0) {
        return true;   /* fail open */
      }

      var thumb = rect.height > 0 ? rect.height : 28;
      if (thumb > rect.width) {
        thumb = rect.width;
      }

      var frac = (val - min) / (max - min);
      if (frac < 0) {
        frac = 0;
      }
      if (frac > 1) {
        frac = 1;
      }

      var centre = rect.left + (thumb / 2) + frac * (rect.width - thumb);
      return Math.abs(clientX - centre) <= (thumb / 2) + SLOP;
    }

    function onDown(e) {
      var el = e.target;
      if (!isSlider(el)) {
        return;
      }
      el.setAttribute('data-drag-start', el.value);
      el.__armed = onThumb(el, pointOf(e));

      /* With a mouse the press can be cancelled outright, which stops the
       * jump before it renders. Touch is deliberately left alone: cancelling
       * touchstart would also cancel any page scrolling that began on the
       * slider, and the restore below makes the value safe either way. */
      if (!el.__armed && e.type === 'mousedown' && e.cancelable) {
        e.preventDefault();
      }
    }

    /* Arrow keys are already a deliberate adjustment. */
    function onKey(e) {
      if (isSlider(e.target)) {
        e.target.__armed = true;
      }
    }

    function onChange(e) {
      var el = e.target;
      if (!isSlider(el) || el.__armed) {
        return;
      }
      var start = el.getAttribute('data-drag-start');
      if (start !== null && start !== undefined) {
        el.value = start;   /* assigning .value fires no event - no loop */
      }
      e.stopPropagation();  /* capture phase: Clay's listeners never run */
      if (e.cancelable) {
        e.preventDefault();
      }
    }

    var useCapture = true;
    document.addEventListener('pointerdown', onDown, useCapture);
    document.addEventListener('touchstart', onDown, useCapture);
    document.addEventListener('mousedown', onDown, useCapture);
    document.addEventListener('keydown', onKey, useCapture);
    document.addEventListener('input', onChange, useCapture);
    document.addEventListener('change', onChange, useCapture);
  });
};
