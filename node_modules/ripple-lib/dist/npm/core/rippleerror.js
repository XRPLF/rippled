'use strict';

var util = require('util');
var extend = require('extend');

function RippleError(code, message) {
  switch (typeof code) {
    case 'object':
      extend(this, code);
      break;

    case 'string':
      this.result = code;
      this.result_message = message;
      break;
  }

  this.engine_result = this.result = this.result || this.engine_result || this.error || 'Error';
  this.engine_result_message = this.result_message = this.result_message || this.engine_result_message || this.error_message || 'Error';
  this.result_message = this.message = this.result_message;

  var stack;

  if (!!Error.captureStackTrace) {
    Error.captureStackTrace(this, code || this);
  } else if (stack = new Error().stack) {
    this.stack = stack;
  }
};

util.inherits(RippleError, Error);

RippleError.prototype.name = 'RippleError';

exports.RippleError = RippleError;