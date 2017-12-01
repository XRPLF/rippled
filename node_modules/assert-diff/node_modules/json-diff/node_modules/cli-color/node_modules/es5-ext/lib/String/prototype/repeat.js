// Not rocket science but taken from:
// http://closure-library.googlecode.com/svn/trunk/closure/goog/string/string.js

'use strict';

var value = require('../../Object/valid-value');

module.exports = function (n) {
	return new Array((isNaN(n) ? 1 : Number(n)) + 1).join(String(value(this)));
};
