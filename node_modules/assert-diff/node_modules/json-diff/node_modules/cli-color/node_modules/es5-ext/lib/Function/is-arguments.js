'use strict';

var toString = Object.prototype.toString

  , id = toString.call(require('./arguments')());

module.exports = function (x) {
	return toString.call(x) === id;
};
