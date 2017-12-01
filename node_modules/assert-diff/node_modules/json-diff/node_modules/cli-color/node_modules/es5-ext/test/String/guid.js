'use strict';

module.exports = function (t, a) {
	a(typeof t(), 'string', "Is string");
	a.not(t(), t(), "Unique");
};
