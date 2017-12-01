'use strict';

var noop = require('../../lib/Function/noop')

  , fn   = function () { return this; };

module.exports = {
	"": function (t, a) {
		var x = {}
		  , o = t({ a: fn, b: null, c: fn, d: 'raz', e: x, f: fn })
		  , f;

		f = o.a;
		a(f(), o, "Bind method");
		a(o.b, null, "Do not change null");
		f = o.c;
		a(f(), o, "Bind other method");
		a(o.d, 'raz', "Do not change primitive");
		a(o.e, x, "Do not change objects");
		f = o.f;
		a(f(), o, "Bind all methods");
	},
	"Custom scope": function (t, a) {
		var scope = {}
		  , f = t({ a: fn }, scope).a;
		a(f(), scope);
	},
	"Custom source": function (t, a) {
		var o = t({ a: noop, c: fn }, null, { a: fn, b: fn })
		  , f;
		f = o.a;
		a(f(), o, "Overwrite");
		f = o.b;
		a(f(), o, "Add from source");
		f = o.c;
		a.not(f(), o, "Do not bind own methods not found in source");
	}
};
