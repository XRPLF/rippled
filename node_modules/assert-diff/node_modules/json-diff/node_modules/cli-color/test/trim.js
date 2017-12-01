'use strict';

var clc = require('../lib');

module.exports = function (t, a) {
	a(t(clc.red('raz') + 'dwa' + clc.bold('trzy')), 'razdwatrzy');
};
