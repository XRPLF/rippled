'use strict';

var indexOf = require('./e-index-of')
  , splice  = Array.prototype.splice;

module.exports = function (item) {
	var index = indexOf.call(this, item);
	if (index !== -1) {
		splice.call(this, index, 1);
	}
};
