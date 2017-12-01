'use strict';

var replace     = String.prototype.replace
  , toUpperCase = function (m, a) { return a.toUpperCase(); };

module.exports = function () {
	return replace.call(this, /-([a-z0-9])/g, toUpperCase);
};
