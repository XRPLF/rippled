'use strict';

var indexOf = require('./e-index-of');

module.exports = function (searchElement) {
	return indexOf.call(this, searchElement, arguments[1]) > -1;
};
