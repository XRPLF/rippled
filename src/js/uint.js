
var sjcl    = require('../../build/sjcl');
var utils   = require('./utils');
var config  = require('./config');
var jsbn    = require('./jsbn');

var BigInteger = jsbn.BigInteger;
var nbi        = jsbn.nbi;

var Base = require('./base').Base;

//
// Abstract UInt class
//
// Base class for UInt??? classes
//

var UInt = function () {
  // Internal form: NaN or BigInteger
  this._value  = NaN;
};

UInt.json_rewrite = function (j, opts) {
  return this.from_json(j).to_json(opts);
};

// Return a new UInt from j.
UInt.from_generic = function (j) {
  if (j instanceof this) {
    return j.clone();
  } else {
    return (new this()).parse_generic(j);
  }
};

// Return a new UInt from j.
UInt.from_hex = function (j) {
  if (j instanceof this) {
    return j.clone();
  } else {
    return (new this()).parse_hex(j);
  }
};

// Return a new UInt from j.
UInt.from_json = function (j) {
  if (j instanceof this) {
    return j.clone();
  } else {
    return (new this()).parse_json(j);
  }
};

// Return a new UInt from j.
UInt.from_bits = function (j) {
  if (j instanceof this) {
    return j.clone();
  } else {
    return (new this()).parse_bits(j);
  }
};

// Return a new UInt from j.
UInt.from_bn = function (j) {
  if (j instanceof this) {
    return j.clone();
  } else {
    return (new this()).parse_bn(j);
  }
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

  return d;
};

UInt.prototype.equals = function (d) {
  return this._value instanceof BigInteger && d._value instanceof BigInteger && this._value.equals(d._value);
};

UInt.prototype.is_valid = function () {
  return this._value instanceof BigInteger;
};

UInt.prototype.is_zero = function () {
  return this._value.equals(BigInteger.ZERO);
};

// value = NaN on error.
UInt.prototype.parse_generic = function (j) {
  // Canonicalize and validate
  if (config.accounts && j in config.accounts)
    j = config.accounts[j].account;

  switch (j) {
  case undefined:
  case "0":
  case this.constructor.STR_ZERO:
  case this.constructor.ADDRESS_ZERO:
  case this.constructor.HEX_ZERO:
    this._value  = nbi();
    break;

  case "1":
  case this.constructor.STR_ONE:
  case this.constructor.ADDRESS_ONE:
  case this.constructor.HEX_ONE:
    this._value  = new BigInteger([1]);

    break;

  default:
    if ('string' !== typeof j) {
	    this._value  = NaN;
    }
    else if (j[0] === "r") {
	    this._value  = Base.decode_check(Base.VER_ACCOUNT_ID, j);
    }
    else if (this.constructor.width === j.length) {
	    this._value  = new BigInteger(utils.stringToArray(j), 256);
    }
    else if ((this.constructor.width*2) === j.length) {
	    // XXX Check char set!
	    this._value  = new BigInteger(j, 16);
    }
    else {
	    this._value  = NaN;
    }
  }

  return this;
};

UInt.prototype.parse_hex = function (j) {
  if ('string' === typeof j &&
      j.length === (this.constructor.width * 2)) {
    this._value  = new BigInteger(j, 16);
  } else {
    this._value  = NaN;
  }

  return this;
};

UInt.prototype.parse_bits = function (j) {
  if (sjcl.bitArray.bitLength(j) !== this.constructor.width * 8) {
    this._value = NaN;
  } else {
    var bytes = sjcl.codec.bytes.fromBits(j);
	  this._value  = new BigInteger(bytes, 256);
  }

  return this;
};

UInt.prototype.parse_json = UInt.prototype.parse_hex;

UInt.prototype.parse_bn = function (j) {
  if (j instanceof sjcl.bn &&
      j.bitLength() <= this.constructor.width * 8) {
    var bytes = sjcl.codec.bytes.fromBits(j.toBits());
	  this._value  = new BigInteger(bytes, 256);
  } else {
    this._value = NaN;
  }

  return this;
};

// Convert from internal form.
UInt.prototype.to_bytes = function () {
  if (!(this._value instanceof BigInteger))
    return null;

  var bytes  = this._value.toByteArray();
  bytes = bytes.map(function (b) { return (b+256) % 256; });
  var target = this.constructor.width;

  // XXX Make sure only trim off leading zeros.
  bytes = bytes.slice(-target);
  while (bytes.length < target) bytes.unshift(0);

  return bytes;
};

UInt.prototype.to_hex = function () {
  if (!(this._value instanceof BigInteger))
    return null;

  var bytes = this.to_bytes();

  return sjcl.codec.hex.fromBits(sjcl.codec.bytes.toBits(bytes)).toUpperCase();
};

UInt.prototype.to_json = UInt.prototype.to_hex;

UInt.prototype.to_bits = function () {
  if (!(this._value instanceof BigInteger))
    return null;

  var bytes = this.to_bytes();

  return sjcl.codec.bytes.toBits(bytes);
};

UInt.prototype.to_bn = function () {
  if (!(this._value instanceof BigInteger))
    return null;

  var bits = this.to_bits();

  return sjcl.bn.fromBits(bits);
};

exports.UInt = UInt;

// vim:sw=2:sts=2:ts=8:et
