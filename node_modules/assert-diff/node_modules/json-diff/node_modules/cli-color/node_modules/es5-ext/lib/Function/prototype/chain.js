'use strict';

var apply    = Function.prototype.apply
  , callable = require('../../Object/valid-callable')
  , toArray  = require('../../Array/from')

  , callFn;

callFn = function (result, fn) {
	return [apply.call(fn, this, result)];
};

module.exports = function () {
	var fns = [this].concat(toArray(arguments));
	fns.forEach(callable);
	return function () {
		return fns.reduce(callFn.bind(this), arguments)[0];
	};
};
