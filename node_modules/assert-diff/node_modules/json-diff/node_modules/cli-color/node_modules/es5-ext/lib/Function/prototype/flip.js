'use strict';

var apply    = Function.prototype.apply
  , callable = require('../../Object/valid-callable')
  , toArray  = require('../../Array/from');

module.exports = function () {
	var fn = callable(this);

	return function (a, b) {
		var args;
		if (arguments.length > 0) {
			args = toArray(arguments);
			args[0] = b;
			args[1] = a;
		} else {
			args = [];
		}
		return apply.call(fn, this, args);
	};
};
