'use strict';

var slice    = Array.prototype.slice
  , apply    = Function.prototype.apply
  , callable = require('../../Object/valid-callable')
  , partial  = require('./partial')

  , hold = function hold(fn, n, args) {
	args = args.concat(slice.call(arguments, 3)).slice(0, n);
	return args.length === n ? apply.call(fn, this, args) :
			hold.partial(fn, n, args);
};

hold.partial = partial;

module.exports = function (n) {
	callable(this);
	return hold.partial(this,
		isNaN(n) ? this.length : Number(n), []);
};
