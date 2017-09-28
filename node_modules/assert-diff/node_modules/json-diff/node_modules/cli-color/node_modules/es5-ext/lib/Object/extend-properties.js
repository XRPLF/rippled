'use strict';

var forEach                  = Array.prototype.forEach
  , slice                    = Array.prototype.slice
  , defineProperties         = Object.defineProperties
  , getOwnPropertyDescriptor = Object.getOwnPropertyDescriptor
  , getOwnPropertyNames      = Object.getOwnPropertyNames
  , value                    = require('./valid-value')
  , extend;

extend = function (properties, src) {
	getOwnPropertyNames(src).forEach(function (key) {
		var desc;
		if (!(desc = getOwnPropertyDescriptor(this, key)) || desc.configurable) {
			properties[key] = getOwnPropertyDescriptor(src, key);
		}
	}, this);
};

module.exports = function (dest, src) {
	var properties;
	forEach.call(arguments, value);
	slice.call(arguments, 1).forEach(extend.bind(dest, properties = {}));
	return defineProperties(dest, properties);
};
