'use strict';

var min    = Math.min
  , toUint = require('../../Number/to-uint')
  , value  = require('../../Object/valid-value');

module.exports = function (searchString) {
	var self, start, endPos;
	self = String(value(this));
	searchString = String(searchString);
	endPos = arguments[1];
	start = ((endPos == null) ? self.length : min(toUint(endPos), self.length))
		- searchString.length;
	return (start < 0) ? false : (self.indexOf(searchString, start) === start);
};
