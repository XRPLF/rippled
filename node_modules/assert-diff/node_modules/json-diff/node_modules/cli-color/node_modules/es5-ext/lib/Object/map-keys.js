'use strict';

var forEach = require('./for-each');

module.exports = function (obj, cb) {
	var o = {}, thisArg = arguments[2];
	forEach(obj, function (value, key) {
		o[cb.call(thisArg, key, value, this)] = value;
	}, obj);
	return o;
};
