'use strict';

var toArray = require('../../../lib/Array/from');

module.exports = function (t, a) {
	return {
		"0": function () {
			var i = 0, fn = function () { ++i; return 3; };

			fn = t.call(fn);
			a(fn(), 3, "First");
			a(fn(1), 3, "Second");
			a(fn(5), 3, "Third");
			a(i, 1, "Called once");
		},
		"1": function () {
			var i = 0, fn = function (x) { ++i; return x; };

			fn = t.call(fn);
			return {
				"No arg": function () {
					i = 0;
					a(fn(), undefined, "First");
					a(fn(), undefined, "Second");
					a(fn(), undefined, "Third");
					a(i, 1, "Called once");
				},
				"Arg": function () {
					var x = {};
					i = 0;
					a(fn(x, 8), x, "First");
					a(fn(x, 4), x, "Second");
					a(fn(x, 2), x, "Third");
					a(i, 1, "Called once");
				},
				"Other Arg": function () {
					var x = {};
					i = 0;
					a(fn(x, 2), x, "First");
					a(fn(x, 9), x, "Second");
					a(fn(x, 3), x, "Third");
					a(i, 1, "Called once");
				}
			};
		},
		"3": function () {
			var i = 0, fn = function (x, y, z) { ++i; return [x, y, z]; }, r;

			fn = t.call(fn);
			return {
				"No args": function () {
					i = 0;
					a.deep(r = fn(), [undefined, undefined, undefined], "First");
					a(fn(), r, "Second");
					a(fn(), r, "Third");
					a(i, 1, "Called once");
				},
				"Some Args": function () {
					var x = {};
					i = 0;
					a.deep(r = fn(x, 8), [x, 8, undefined], "First");
					a(fn(x, 8), r, "Second");
					a(fn(x, 8), r, "Third");
					a(i, 1, "Called once");
					return {
						"Other": function () {
							a.deep(r = fn(x, 5), [x, 5, undefined], "Second");
							a(fn(x, 5), r, "Third");
							a(i, 2, "Called once");
						}
					};
				},
				"Full stuff": function () {
					var x = {};
					i = 0;
					a.deep(r = fn(x, 8, 23, 98), [x, 8, 23], "First");
					a(fn(x, 8, 23, 43), r, "Second");
					a(fn(x, 8, 23, 9), r, "Third");
					a(i, 1, "Called once");
					return {
						"Other": function () {
							a.deep(r = fn(x, 23, 8, 13), [x, 23, 8], "Second");
							a(fn(x, 23, 8, 22), r, "Third");
							a(i, 2, "Called once");
						}
					};
				}
			};
		},
		"Dynamic": function () {
			var i = 0, fn = function () { ++i; return arguments; }, r;

			fn = t.call(fn, false);
			return {
				"No args": function () {
					i = 0;
					a.deep(toArray(r = fn()), [], "First");
					a(fn(), r, "Second");
					a(fn(), r, "Third");
					a(i, 1, "Called once");
				},
				"Some Args": function () {
					var x = {};
					i = 0;
					a.deep(toArray(r = fn(x, 8)), [x, 8], "First");
					a(fn(x, 8), r, "Second");
					a(fn(x, 8), r, "Third");
					a(i, 1, "Called once");
				},
				"Many args": function () {
					var x = {};
					i = 0;
					a.deep(toArray(r = fn(x, 8, 23, 98)), [x, 8, 23, 98], "First");
					a(fn(x, 8, 23, 98), r, "Second");
					a(fn(x, 8, 23, 98), r, "Third");
					a(i, 1, "Called once");
				}
			};
		},
		"Original arguments": function (a) {
			var fn, mfn, x = {};
			fn = function (x, y) { return toArray(mfn.args); };
			mfn = t.call(fn);

			a.deep(mfn(23, 'raz', x), [23, 'raz', x]);
		},
		"Resolvers": function () {
			var i = 0, fn, fn2, r, j = 0, z;
			fn = t.call(function () { ++i; return arguments; },
				 3, [Boolean, String]);
			return {
				"No args": function () {
					i = 0;
					a.deep(toArray(r = fn()), [false, 'undefined'], "First");
					a(fn(), r, "Second");
					a(fn(), r, "Third");
					a(i, 1, "Called once");
				},
				"Some Args": function () {
					var x = {};
					i = 0;
					a.deep(toArray(r = fn(0, 34, x, 45)), [false, '34', x, 45],
						"First");
					a(fn(0, 34, x, 22), r, "Second");
					a(fn(0, 34, x, false), r, "Third");
					a(i, 1, "Called once");
					return {
						"Other": function () {
							a.deep(toArray(r = fn(1, 34, x, 34)),
								[true, '34', x, 34], "Second");
							a(fn(1, 34, x, 89), r, "Third");
							a(i, 2, "Called once");
						}
					};
				}
			};
		},
		"Clear Cache": {
			"Specific": function () {
				var i = 0, fn, mfn, r, x = {};

				fn = function (a, b, c) {
					if (c === 3) {
						++i;
					}
					return arguments;
				}

				mfn = t.call(fn);
				mfn(1, x, 3);
				mfn(1, x, 4);
				mfn.clearCache(1, x, 4);
				mfn(1, x, 3);
				mfn(1, x, 3);
				a(i, 1, "Pre clear");
				mfn.clearCache(1, x, 3);
				mfn(1, x, 3);
				a(i, 2, "After clear");

				i = 0;
				mfn = t.call(fn, false);
				mfn(1, x, 3);
				mfn(1, x, 3);
				mfn();
				mfn();
				mfn.clearCache();
				mfn(1, x, 3);
				a(i, 1, "Proper no arguments clear");
			},
			"All": function () {
				var i = 0, fn, r, x = {};

				fn = function (a, b, c) {
					++i;
					return arguments;
				}

				fn = t.call(fn);
				fn(1, x, 3);
				fn(1, x, 4);
				fn(1, x, 3);
				fn(1, x, 4);
				a(i, 2, "Pre clear");
				fn.clearAllCache();
				fn(1, x, 3);
				fn(1, x, 4);
				fn(1, x, 3);
				fn(1, x, 4);
				a(i, 4, "After clear");
			}
		}
	};
};
