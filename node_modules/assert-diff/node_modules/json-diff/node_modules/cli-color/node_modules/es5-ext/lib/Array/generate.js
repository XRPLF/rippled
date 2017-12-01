// Generate an array built of repeated arguments

'use strict';

var slice = Array.prototype.slice
  , value = require('../Object/valid-value')

module.exports = function (length, fill) {
	var arr, l;
	length = value(length) >>> 0;
	if (length === 0) {
		return [];
	}
	arr = (arguments.length < 2) ? [undefined] :
		slice.call(arguments, 1, 1 + length);

	while ((l = arr.length) < length) {
		arr = arr.concat(arr.slice(0, length - l));
	}
	return arr;
};
