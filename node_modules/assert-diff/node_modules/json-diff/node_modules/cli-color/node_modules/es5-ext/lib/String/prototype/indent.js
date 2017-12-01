'use strict';

var replace = String.prototype.replace
  , repeat  = require('./repeat')

  , re = /(\r\n|[\n\r\u2028\u2029])([\u0000-\u0009\u000b-\uffff]+)/g;

module.exports = function (indent, count) {
	indent = repeat.call(String(indent), count);
	return indent + replace.call(this, re, '$1' + indent + '$2');
};
