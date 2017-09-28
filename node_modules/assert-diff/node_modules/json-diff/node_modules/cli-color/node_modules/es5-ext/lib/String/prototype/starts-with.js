'use strict';

var indexOf = String.prototype.indexOf
  , toUint  = require('../../Number/to-uint');

module.exports = function (searchString) {
	var start = toUint(arguments[1]);
	return (indexOf.call(this, searchString, start) === start);
};
