'use strict';

var apply          = Function.prototype.apply
  , callable = require('../../Object/valid-callable');

module.exports = function () {
	var fn = callable(this);
	return function () {
		try {
			return apply.call(fn, this, arguments);
		} catch (e) {
			return e;
		}
	};
};
