'use strict';

var isArray       = Array.isArray
  , slice         = Array.prototype.slice
  , isArguments   = require('../Function/is-arguments');

module.exports = function (obj) {
	if (isArray(obj)) {
		return obj;
	} else if (isArguments(obj)) {
		return (obj.length === 1) ? [obj[0]] : Array.apply(null, obj);
	} else {
		return slice.call(obj);
	}
};
