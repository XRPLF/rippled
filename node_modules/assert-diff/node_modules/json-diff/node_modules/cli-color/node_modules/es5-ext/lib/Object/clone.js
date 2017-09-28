'use strict';

var create                   = Object.create
  , getOwnPropertyDescriptor = Object.getOwnPropertyDescriptor
  , getOwnPropertyNames      = Object.getOwnPropertyNames
  , getPrototypeOf           = Object.getPrototypeOf;

module.exports = function (obj) {
	var props = {};
	getOwnPropertyNames(obj).forEach(function (name) {
		props[name] = getOwnPropertyDescriptor(obj, name);
	}, obj);
	return create(getPrototypeOf(obj), props);
};
