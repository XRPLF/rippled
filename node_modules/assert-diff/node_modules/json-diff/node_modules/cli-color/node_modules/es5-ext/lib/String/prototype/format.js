'use strict';

var call       = Function.prototype.call
  , replace    = String.prototype.replace
  , isCallable = require('../../Object/is-callable')
  , value      = require('../../Object/valid-value');

module.exports = function (fmap) {
	var t, fn, thisArg = arguments[1];
	value(this);
	fmap = Object(value(fmap));
	return replace.call(this, /%([a-zA-Z]+)|\\([\u0000-\uffff])/g,
		function (match, token, escape) {
			if (escape) {
				return escape;
			} else {
				t = token;
				while (t && !(fn = fmap[t])) {
					t = t.slice(0, -1);
				}
				return fn ? (isCallable(fn) ? call.call(fn, thisArg) : fn)
					+ token.slice(t.length) : match;
			}
		});
};
