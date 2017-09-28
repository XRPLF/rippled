'use strict';

var call     = Function.prototype.call
  , callable = require('../../Object/valid-callable')
  , value    = require('../../Object/valid-value');

module.exports = function (cb) {
	var i, self, thisArg;

	self = Object(value(this));
	callable(cb);
	thisArg = arguments[1];

	for (i = self.length >>> 0; i >= 0; --i) {
		if (self.hasOwnProperty(i)) {
			call.call(cb, thisArg, self[i], i, self);
		}
	}
};
