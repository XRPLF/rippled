'use strict';

var defineProperties = Object.defineProperties
  , map              = require('es5-ext/lib/Object/map')

  , toString, codes, properties, init;

toString = function (code, str) {
	return '\x1b[' + code[0] + 'm' + (str || "") + '\x1b[' + code[1] + 'm';
};

codes = {
	// styles
	bold:      [1, 22],
	italic:    [3, 23],
	underline: [4, 24],
	inverse:   [7, 27],
	strike:    [9, 29]
};

['black', 'red', 'green', 'yellow', 'blue', 'magenta', 'cyan', 'white'].forEach(
	function (color, index) {
		// foreground
		codes[color] = [30 + index, 39];
		// background
		codes['bg' + color[0].toUpperCase() + color.slice(1)] = [40 + index, 49];
	}
);
codes.gray = [90, 39];

properties = map(codes, function (code) {
	return {
		get: function () {
			this.style.push(code);
			return this;
		},
		enumerable: true
	};
});
properties.bright = {
	get: function () {
		this._bright = true;
		return this;
	},
	enumerable: true
};
properties.bgBright = {
	get: function () {
		this._bgBright = true;
		return this;
	},
	enumerable: true
};

init = function () {
	var o = defineProperties(function self(msg) {
		return self.style.reduce(function (msg, code) {
			if ((self._bright && (code[0] >= 30) && (code[0] < 38)) ||
					(self._bgBright && (code[0] >= 40) && (code[0] < 48))) {
				code = [code[0] + 60, code[1]];
			}
			return toString(code, msg);
		}, msg);
	}, properties);
	o.style = [];
	return o[this];
};

module.exports = defineProperties(function (msg) {
	return msg;
}, map(properties, function (code, name) {
	return {
		get: init.bind(name),
		enumerable: true
	};
}));
