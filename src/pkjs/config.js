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
          'Nudge each row up or down. Negative moves a row up, positive ' +
          'moves it down. Changes apply straight away.'
      },
      offsetSlider('OffMinute', 'Minute number',
                   'Top row - "quarter", "twenty-seven", "half"'),
      offsetSlider('OffMinutes', '"minutes"',
                   'The small annotation under the minute number'),
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
