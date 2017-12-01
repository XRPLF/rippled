'use strict';

var lastIndexOf = Array.prototype.lastIndexOf
  , isNaN       = require('../../Number/is-nan')
  , ois         = require('../../Object/is')
  , value       = require('../../Object/valid-value');

module.exports = function (searchElement) {
	var i, fromIndex;
	if (!isNaN(searchElement) && (searchElement !== 0)) {
		return lastIndexOf.apply(this, arguments);
	}

	value(this);
	fromIndex = Number(arguments[1]);
	fromIndex = isNaN(fromIndex) ? ((this.length >>> 0) - 1) : fromIndex;
	for (i = fromIndex; i >= 0; --i) {
		if (this.hasOwnProperty(i) && ois(searchElement, this[i])) {
			return i;
		}
	}
	return -1;
};
