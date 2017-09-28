'use strict';

var apply    = Function.prototype.apply
  , callable = require('../../Object/valid-callable');

module.exports = function () {
	var fn = callable(this)
	  , args = arguments;

	return function () {
		return apply.call(fn, this, args);
	};
};
