'use strict';

module.exports = function (t, a) {
	var x = {};
	a(t.call(3), 3, "Primitive");
	a(t.call(x), x, "Object");
};
