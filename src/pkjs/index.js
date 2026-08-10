var Clay = require('pebble-clay');
var clayConfig = require('./config');
var customClay = require('./custom-clay');

// Clay handles 'showConfiguration' and 'webviewclosed' itself and forwards the
// saved settings to the watch as an AppMessage.
//
// The second argument runs inside the settings page itself - Clay injects its
// source into the generated HTML. See custom-clay.js; it makes the offset
// sliders drag-only so scrolling past one cannot nudge a row.
var clay = new Clay(clayConfig, customClay);
