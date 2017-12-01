// Trim formatting from string

'use strict';

var r = new RegExp('\x1b\\[\\d{1,2}m', 'g');

module.exports = function (str) {
	return str.replace(r, '');
};
