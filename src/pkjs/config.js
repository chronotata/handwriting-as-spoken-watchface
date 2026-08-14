// Settings page shown in the Pebble phone app.
//
// The offset sliders exist so the layout can be nudged by eye on the real
// watch. They apply immediately - no rebuild, no reinstall.
//
// Font sizes are NOT here: Pebble rasterises a TTF into a bitmap font at build
// time, taking the size from the trailing number in the resource name, so a
// size cannot be changed at runtime. To change one, edit src/c/config.h, run
// tools/tune.py, and rebuild.

function offsetSlider(messageKey, label, description) {
  return {
    type: 'slider',
    messageKey: messageKey,
    label: label,
    description: description,
    defaultValue: 0,
    min: -15,
    max: 15,
    step: 1
  };
}

module.exports = [
  {
    type: 'heading',
    defaultValue: 'Handwritten'
  },
  {
    type: 'text',
    defaultValue:
      'Tells the time the way it is spoken: "quarter past three", ' +
      '"two to six", "midnight".'
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Colours' },
      {
        type: 'color',
        messageKey: 'PaperColor',
        defaultValue: '000000',
        label: 'Paper',
        sunlight: true
      },
      {
        type: 'color',
        messageKey: 'InkColor',
        defaultValue: 'FFFFFF',
        label: 'Ink',
        sunlight: true
      },
      // Only does anything when the ink is darker than the paper. Light
      // strokes bloom on this screen and dark ones do not, so dark-on-light
      // measured 19.7% thinner on the real watch; this puts a slightly
      // bolder outline back. See BOLD_BLEND in tools/tune.py.
      // A dropdown rather than a slider because the screen has four levels
      // per channel, so the sixteen weights collapse to four visibly
      // distinct ones for black on white: 0-2, 3-7, 8-12, 13-15. The values
      // below sit in the middle of each band. A slider offered fifteen
      // steps, twelve of which did nothing.
      {
        type: 'select',
        messageKey: 'StrokeWeight',
        label: 'Stroke weight',
        description:
          'Only applies when the ink is darker than the paper — dark text ' +
          'renders about 20% thinner than light text on this screen. ' +
          'Medium is the measured match; Solid is bolder but the edges go ' +
          'crunchy.',
        defaultValue: '10',
        options: [
          { label: 'Off', value: '0' },
          { label: 'Light', value: '5' },
          { label: 'Medium', value: '10' },
          { label: 'Solid', value: '14' }
        ]
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Wording' },
      // Values are indices into kMinutesModes[] in src/c/handwritten.c and
      // are the wire format - never reordered, new modes on the end.
      // Values index kRoundingModes[] in src/c/handwritten.c - wire format,
      // never reordered. "Nearest five" is how most people read an analogue
      // dial; it also stops "minute(s)" ever appearing, which is the point.
      {
        type: 'select',
        messageKey: 'Rounding',
        label: 'Read the minute',
        description:
          'Rounding to the nearest five is how an analogue dial is usually ' +
          'read aloud. "Spoken" adds <em>just gone</em> or <em>nearly</em> ' +
          'to say which way it rounded. Note that at :58 and :59 the hour ' +
          'rolls forward while the date stays on today.',
        defaultValue: '0',
        options: [
          { label: 'Exact minute', value: '0' },
          { label: 'Nearest five', value: '1' },
          { label: 'Nearest five, spoken', value: '2' }
        ]
      },
      {
        type: 'select',
        messageKey: 'MinutesText',
        label: 'Say "minutes"',
        defaultValue: '0',
        options: [
          { label: 'Only when not a round five', value: '0' },
          { label: 'Never', value: '1' },
          { label: 'After any number', value: '2' }
        ]
      },
      {
        type: 'text',
        defaultValue:
          '"Only when not a round five" gives <em>twenty-nine minutes to ' +
          'ten</em> but <em>five past five</em>. "Never" drops it ' +
          'everywhere. "After any number" says <em>five minutes past ' +
          'five</em>, while still saying <em>quarter past</em> and ' +
          '<em>half past</em> — "quarter minutes past" is not English.' +
          ' With rounding on, "only when not a round five" never fires, ' +
          'since every minute becomes one.'
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Date' },
      {
        type: 'toggle',
        messageKey: 'ShowDate',
        label: 'Show the date',
        defaultValue: true
      },
      // The values are indices into kDateFormats[] in src/c/handwritten.c and
      // are the wire format, so they must not be reordered or renumbered -
      // a new format goes on the end of both lists. Clay sends a select as a
      // STRING; tuple_int() on the C side accepts either representation.
      {
        type: 'select',
        messageKey: 'DateFormat',
        label: 'Date format',
        defaultValue: '0',
        options: [
          { label: '10th Aug. 2026', value: '0' },
          { label: 'Mon. 10th Aug.', value: '1' }
        ]
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Fine tuning' },
      {
        type: 'text',
        defaultValue:
          '<strong>Zero is the tuned layout</strong>, not a flat one. ' +
          'Each slider nudges its row away from where the watchface was ' +
          'designed to put it, so putting one back to 0 restores the ' +
          'design rather than undoing it.<br><br>Negative moves a row up, ' +
          'positive moves it down. Changes apply straight away. ' +
          '<strong>Drag the dot</strong> — tapping the track does nothing, ' +
          'so scrolling past a slider cannot move it by accident. The box ' +
          'on the right takes a typed number.<br><br>Anything that cannot ' +
          'affect what is currently on the watch is greyed out — the ' +
          'wording and date settings above decide which rows get drawn.'
      },
      // The two rounded modes draw the same phrase and differ only in where
      // the whole of it sits, so each gets its own lever and neither drags
      // the other. See OFFSET_BLOCK / OFFSET_BLOCK_FIVE in src/c/config.h.
      offsetSlider('OffBlock', 'Whole phrase, with "just gone" / "nearly"',
                   'Moves the minute, "past"/"to" and the hour together, ' +
                   'leaving the hedge, the date and the on-the-hour wording ' +
                   'where they are. This is the phrase sitting lower to ' +
                   'leave the hedge its room.'),
      offsetSlider('OffBlockFive', 'Whole phrase, without the hedge',
                   'The same phrase in plain "nearest five", where nothing ' +
                   'sits above it and it can ride higher. Tuned separately ' +
                   'so adjusting one reading mode never moves the other.'),
      offsetSlider('OffHedge', '"just gone" / "nearly"',
                   'The small word above the minute.'),
      offsetSlider('OffHedgeSolo', '"just gone" / "nearly", on the hour',
                   'The same word above the o\'clock wording, which is a ' +
                   'different balance.'),
      offsetSlider('OffSplitHead', 'Split number, top half',
                   '"twenty-" — only appears when the minute splits across ' +
                   'two lines. Use this to pull it clear of the top edge ' +
                   'without moving anything below it.'),
      offsetSlider('OffMinute', 'Minute number, with "minute(s)" below',
                   ':01–:20 when the annotation is shown — it fills the ' +
                   'space down to "past"/"to"'),
      offsetSlider('OffMinuteAlone', 'Minute number, alone',
                   '"quarter", "half", or any number with no annotation. ' +
                   'Nothing sits between it and "past"/"to", so the gap ' +
                   'beneath is empty'),
      offsetSlider('OffMinuteSplit', 'Split number, lower half',
                   'the "one" of "twenty-one", with or without "minute(s)" ' +
                   'beside it'),
      offsetSlider('OffMinutes', '"minute(s)" beside the number',
                   ':21–:29, riding next to the split. Set this to the ' +
                   'SAME value as "Split number, lower half" to keep the ' +
                   'two level — that is the row it sits beside — and ' +
                   'differ them only deliberately'),
      offsetSlider('OffMinutesOwn', '"minute(s)" on its own line',
                   ':01–:20, where it has a line to itself. Nothing holds ' +
                   'it to another word, so this is purely how close it sits ' +
                   'to "past"/"to"'),
      offsetSlider('OffRelation', '"past" / "to"',
                   'Middle row - the anchor the phrase hangs from'),
      offsetSlider('OffHour', 'Hour word',
                   'Bottom row - "six", "midnight", "noon"'),
      offsetSlider('OffSolo', '"midnight" / "midday"',
                   'The large word shown on its own at 12'),
      offsetSlider('OffDate', 'Date line', 'The whole date line')
    ]
  },
  {
    type: 'submit',
    defaultValue: 'Save'
  }
];
