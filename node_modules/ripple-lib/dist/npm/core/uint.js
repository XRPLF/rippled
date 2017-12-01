'use strict';

/* eslint new-cap: 1 */

var assert = require('assert');
var lodash = require('lodash');
var sjclcodec = require('sjcl-codec');
var utils = require('./utils');
var BN = require('bn.js');

//
// Abstract UInt class
//
// Base class for UInt classes
//

function UInt() {
  // Internal form: NaN or BN
  this._value = NaN;
}

UInt.json_rewrite = function (j, opts) {
  return this.from_json(j).to_json(opts);
};

// Return a new UInt from j.
UInt.from_generic = function (j) {
  if (j instanceof this) {
    return j.clone();
  }

  return new this().parse_generic(j);
};

// Return a new UInt from j.
UInt.from_hex = function (j) {
  if (j instanceof this) {
    return j.clone();
  }

  return new this().parse_hex(j);
};

// Return a new UInt from j.
UInt.from_json = function (j) {
  if (j instanceof this) {
    return j.clone();
  }

  return new this().parse_json(j);
};

// Return a new UInt from j.
UInt.from_bits = function (j) {
  if (j instanceof this) {
    return j.clone();
  }

  return new this().parse_bits(j);
};

// Return a new UInt from j.
UInt.from_bytes = function (j) {
  if (j instanceof this) {
    return j.clone();
  }

  return new this().parse_bytes(j);
};

// Return a new UInt from j.
UInt.from_number = function (j) {
  if (j instanceof this) {
    return j.clone();
  }

  return new this().parse_number(j);
};

UInt.is_valid = function (j) {
  return this.from_json(j).is_valid();
};

UInt.prototype.clone = function () {
  return this.copyTo(new this.constructor());
};

// Returns copy.
UInt.prototype.copyTo = function (d) {
  d._value = this._value;

  if (this._version_byte !== undefined) {
    d._version_byte = this._version_byte;
  }

  if (typeof d._update === 'function') {
    d._update();
  }

  return d;
};

UInt.prototype.equals = function (o) {
  return this.is_valid() && o.is_valid() &&
  // This throws but the expression will short circuit
  this.cmp(o) === 0;
};

UInt.prototype.cmp = function (o) {
  assert(this.is_valid() && o.is_valid());
  return this._value.cmp(o._value);
};

UInt.prototype.greater_than = function (o) {
  return this.cmp(o) > 0;
};

UInt.prototype.less_than = function (o) {
  return this.cmp(o) < 0;
};

UInt.prototype.is_valid = function () {
  return this._value instanceof BN;
};

UInt.prototype.is_zero = function () {
  // cmpn means cmp with N)umber
  return this.is_valid() && this._value.cmpn(0) === 0;
};

/**
 * Update any derivative values.
 *
 * This allows subclasses to maintain caches of any data that they derive from
 * the main _value. For example, the Currency class keeps the currency type, the
 * currency code and other information about the currency cached.
 *
 * The reason for keeping this mechanism in this class is so every subclass can
 * call it whenever it modifies the internal state.
 *
 * @return {void}
 */
UInt.prototype._update = function () {
  // Nothing to do by default. Subclasses will override this.
};

// value = NaN on error.
UInt.prototype.parse_generic = function (j) {
  var subclass = this.constructor;

  assert(typeof subclass.width === 'number', 'UInt missing width');

  this._value = NaN;

  switch (j) {
    case undefined:
    case '0':
    case subclass.STR_ZERO:
    case subclass.ACCOUNT_ZERO:
    case subclass.HEX_ZERO:
      this._value = new BN(0);
      break;

    case '1':
    case subclass.STR_ONE:
    case subclass.ACCOUNT_ONE:
    case subclass.HEX_ONE:
      this._value = new BN(1);
      break;

    default:
      if (lodash.isString(j)) {
        switch (j.length) {
          case subclass.width:
            var hex = utils.arrayToHex(utils.stringToArray(j));
            this._value = new BN(hex, 16);
            break;
          case subclass.width * 2:
            // Assume hex, check char set
            this.parse_hex(j);
            break;
        }
      } else if (lodash.isNumber(j)) {
        this.parse_number(j);
      } else if (lodash.isArray(j)) {
        // Assume bytes array
        this.parse_bytes(j);
      }
  }

  this._update();

  return this;
};

UInt.prototype.parse_hex = function (j) {
  if (new RegExp('^[0-9A-Fa-f]{' + this.constructor.width * 2 + '}$').test(j)) {
    this._value = new BN(j, 16);
  } else {
    this._value = NaN;
  }

  this._update();

  return this;
};

UInt.prototype.parse_bits = function (j) {
  return this.parse_bytes(sjclcodec.bytes.fromBits(j));
};

UInt.prototype.parse_bytes = function (j) {
  if (Array.isArray(j) && j.length === this.constructor.width) {
    this._value = new BN(j);
  } else {
    this._value = NaN;
  }

  this._update();

  return this;
};

UInt.prototype.parse_json = UInt.prototype.parse_hex;

UInt.prototype.parse_number = function (j) {
  this._value = NaN;

  if (typeof j === 'number' && isFinite(j) && j >= 0) {
    this._value = new BN(j);
  }

  this._update();

  return this;
};

// Convert from internal form.
UInt.prototype.to_bytes = function () {
  if (!this.is_valid()) {
    return null;
  }

  return this._value.toArray('be', this.constructor.width);
};

UInt.prototype.to_hex = function () {
  if (!this.is_valid()) {
    return null;
  }

  return utils.arrayToHex(this.to_bytes());
};

UInt.prototype.to_json = UInt.prototype.to_hex;

// Convert from internal form.
UInt.prototype.to_bits = function () {
  if (!this.is_valid()) {
    return null;
  }

  return sjclcodec.bytes.toBits(this.to_bytes());
};

exports.UInt = UInt;

// vim:sw=2:sts=2:ts=8:et