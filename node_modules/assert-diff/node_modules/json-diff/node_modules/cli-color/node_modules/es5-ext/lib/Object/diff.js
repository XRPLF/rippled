'use strict';

var keys         = Object.keys
  , diff         = require('../Array/prototype/diff')
  , intersection = require('../Array/prototype/intersection')
  , isObject     = require('./is-object');

module.exports = function (obj, target) {
	var k1 = keys(obj), k2 = keys(target);
	return [diff.call(k1, k2), intersection.call(k1, k2).filter(function (key) {
		return (isObject(obj[key]) && isObject(target[key])) ?
				(obj[key].valueOf() !== target[key].valueOf()) :
				(obj[key] !== target[key]);
	}, obj), diff.call(k2, k1)];
};
