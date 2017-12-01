'use strict';

var keys = Object.keys

  , get = function (key) {
		return this[key];
	};

module.exports = function (obj) {
	return keys(obj).map(get, obj);
};
