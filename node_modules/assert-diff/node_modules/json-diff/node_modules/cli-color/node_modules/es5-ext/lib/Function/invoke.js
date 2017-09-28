'use strict';

var slice      = Array.prototype.slice
  , apply      = Function.prototype.apply
  , isCallable = require('../Object/is-callable')
  , value      = require('../Object/valid-value')
  , memoize    = require('./prototype/memoize');

module.exports = memoize.call(function (name) {
	var args, isFn;

	args = slice.call(arguments, 1)
	if (!(isFn = isCallable(name))) {
		name = String(name);
	}

	return function (obj) {
		return apply.call(isFn ? name : value(obj)[name], obj,
			args.concat(slice.call(arguments, 1)));
	};
}, false);
