'use strict';

var getPrototypeOf = Object.getPrototypeOf
  , prototype      = Object.prototype
  , toString       = prototype.toString

  , id = {}.toString();

module.exports = function (value) {
	return (value && (typeof value === 'object') &&
		(getPrototypeOf(value) === prototype) && (toString.call(value) === id)) ||
		false;
};
