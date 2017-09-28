'use strict';

var keys  = Object.keys
  , value = require('./valid-value');

module.exports = function (a, b) {
	var aKeys;
	a = Object(value(a));
	if (b == null) {
		return false;
	}
	b = Object(b);
	if (a === b) {
		return true;
	}
	return ((aKeys = keys(a)).length === keys(b).length) &&
		aKeys.every(function (name) {
			return b.propertyIsEnumerable(name) && (a[name] === b[name]);
		}, a);
};
