'use strict';

var filter  = Array.prototype.filter
  , indexOf = require('./e-index-of')

  , isFirst;

isFirst = function (value, index) {
	return indexOf.call(this, value) === index;
};

module.exports = function () {
	return filter.call(this, isFirst, this);
};
