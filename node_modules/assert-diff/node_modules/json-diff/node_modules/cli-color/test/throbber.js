'use strict';

var spawn = require('child_process').spawn
  , pg = __dirname + '/__playground';

module.exports = {
	"": function (a, d) {
		var t = spawn(pg + '/throbber.js')
		  , out = [], err = '', time = Date.now();

		t.stdout.on('data', function (data) {
			out.push(data);
		});
		t.stderr.on('data', function (data) {
			err += data;
		});
		t.on('exit', function () {
			a.ok(out.length > 4, "Interval");
			a(out.join(""), "START-\b\\\b|\b/\b-\b", "Output");
			a(err, "", "No stderr output");
			d();
		});
	},
	"Formatted": function (a, d) {
		var t = spawn(pg + '/throbber.formatted.js')
		  , out = [], err = '', time = Date.now();

		t.stdout.on('data', function (data) {
			out.push(data);
		});
		t.stderr.on('data', function (data) {
			err += data;
		});
		t.on('exit', function () {
			a.ok(out.length > 4, "Interval");
			a(out.join(""), "START\x1b[31m-\x1b[39m\b\x1b[31m\\\x1b[39m\b\x1b[31m" +
				"|\x1b[39m\b\x1b[31m/\x1b[39m\b\x1b[31m-\x1b[39m\b", "Output");
			a(err, "", "No stderr output");
			d();
		});
	}
};
