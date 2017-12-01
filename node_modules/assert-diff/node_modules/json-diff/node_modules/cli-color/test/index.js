'use strict';

var msg = 'test test';

module.exports = function (t, a) {
	a(t("test"), "test", "Plain");
	a(t.red(msg), '\x1b[31m' + msg + '\x1b[39m', "Foreground");
	a(t.bgRed(msg), '\x1b[41m' + msg + '\x1b[49m', "Background");
	a(t.bold(msg), '\x1b[1m' + msg + '\x1b[22m', "Format");
	a(t.bold.blue(msg), '\x1b[34m\x1b[1m' + msg + '\x1b[22m\x1b[39m',
		"Foreground & Format");
};
