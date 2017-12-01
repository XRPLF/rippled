'use strict';

var getOwnPropertyNames = Object.getOwnPropertyNames
  , getPrototypeOf      = Object.getPrototypeOf
  , push                = Array.prototype.push
  , uniq                = require('../Array/prototype/uniq');

module.exports = function (obj) {
	var keys = getOwnPropertyNames(obj);
	while ((obj = getPrototypeOf(obj))) {
		push.apply(keys, getOwnPropertyNames(obj));
	}
	return uniq.call(keys);
};
