'use strict';

module.exports = function (t, a) {
	var y = {}, z = {}, x = [9, z, 5, y, 'foo'];
	t.call(x, y);
	a.deep(x, [9, z, 5, 'foo']);
	t.call(x, {});
	a.deep(x, [9, z, 5, 'foo'], "Not existing");
	t.call(x, 5);
	a.deep(x, [9, z, 'foo'], "Primitive");
};
