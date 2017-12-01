'use strict';

module.exports = function (t, a) {
	var o1, o2, o3;
	o1 = {};
	o2 = Object.create(o1, {
		raz: {
			value: 1
		},
		dwa: {
			value: 2,
			configurable: true
		}
	});

	o3 = t(o2);

	a(Object.getPrototypeOf(o3), o1, "Prototype");
	a.deep(Object.keys(o3), [], "Keys");
	a.deep(Object.getOwnPropertyDescriptor(o3, 'raz'), { value: 1,
		writable: false, enumerable: false, configurable: false });
	a.deep(Object.getOwnPropertyDescriptor(o3, 'dwa'), { value: 2,
		writable: false, enumerable: false, configurable: true });
};
