var Clay = require('pebble-clay');
var clayConfig = require('./config');

// Clay handles 'showConfiguration' and 'webviewclosed' itself and forwards the
// saved settings to the watch as an AppMessage.
var clay = new Clay(clayConfig);
