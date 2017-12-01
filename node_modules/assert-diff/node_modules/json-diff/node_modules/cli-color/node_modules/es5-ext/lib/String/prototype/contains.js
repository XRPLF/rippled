'use strict';

var indexOf = String.prototype.indexOf;

module.exports = function (searchString) {
	return indexOf.call(this, searchString, arguments[1]) > -1;
};
