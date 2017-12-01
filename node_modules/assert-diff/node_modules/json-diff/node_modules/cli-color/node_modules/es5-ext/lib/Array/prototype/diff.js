'use strict';

var filter   = Array.prototype.filter
  , forEach  = Array.prototype.forEach
  , value    = require('../../Object/valid-value')
  , copy     = require('./copy')
  , contains = require('./contains')
  , remove   = require('./remove');

module.exports = function (other) {
	var r;
	if ((value(this).length >>> 0) > (value(other).length >>> 0)) {
		r = copy.call(this);
		forEach.call(other, remove.bind(r));
		return r;
	} else {
		return filter.call(this, function (item) {
			return !contains.call(other, item);
		});
	}
};
