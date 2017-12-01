'use strict';

var value   = require('../Object/valid-value')
  , memoize = require('./prototype/memoize');

module.exports = memoize.call(function (name) {
	return function (obj) {
		delete value(obj)[name];
	};
}, [function (name) {
	return String(name);
}]);
