'use strict';

var max   = Math.max
  , toInt = require('./to-int');

module.exports = function (value) {
	return max(0, toInt(value));
};
