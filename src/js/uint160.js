
var utils   = require('./utils');
var config  = require('./config');
var jsbn    = require('./jsbn');

var BigInteger = jsbn.BigInteger;
var nbi        = jsbn.nbi;

var Base = require('./base').Base;

//
// UInt160 support
//

var UInt160 = function () {
  // Internal form: NaN or BigInteger
  this._value  = NaN;
};

UInt160.ZERO     = utils.hexToString("0000000000000000000000000000000000000000");
UInt160.ONE      = utils.hexToString("0000000000000000000000000000000000000001");
var ADDRESS_ZERO = UInt160.ADDRESS_ZERO = "rrrrrrrrrrrrrrrrrrrrrhoLvTp";
var ADDRESS_ONE  = UInt160.ADDRESS_ONE = "rrrrrrrrrrrrrrrrrrrrBZbvji";
var HEX_ZERO     = UInt160.HEX_ZERO = "0000000000000000000000000000000000000000";
var HEX_ONE      = UInt160.HEX_ONE = "0000000000000000000000000000000000000001";

UInt160.json_rewrite = function (j) {
  return UInt160.from_json(j).to_json();
};

// Return a new UInt160 from j.
UInt160.from_generic = function (j) {
  return 'string' === typeof j
      ? (new UInt160()).parse_generic(j)
      : j.clone();
};

// Return a new UInt160 from j.
UInt160.from_json = function (j) {
  if ('string' === typeof j) {
    return (new UInt160()).parse_json(j);
  } else if (j instanceof UInt160) {
    return j.clone();
  } else {
    return new UInt160();
  }
};

UInt160.is_valid = function (j) {
  return UInt160.from_json(j).is_valid();
};

UInt160.prototype.clone = function () {
  return this.copyTo(new UInt160());
};

// Returns copy.
UInt160.prototype.copyTo = function (d) {
  d._value = this._value;

  return d;
};

UInt160.prototype.equals = function (d) {
  return this._value instanceof BigInteger && d._value instanceof BigInteger && this._value.equals(d._value);
};

UInt160.prototype.is_valid = function () {
  return this._value instanceof BigInteger;
};

// value = NaN on error.
UInt160.prototype.parse_generic = function (j) {
  // Canonicalize and validate
  if (config.accounts && j in config.accounts)
    j = config.accounts[j].account;

  switch (j) {
    case undefined:
    case "0":
    case UInt160.ZERO:
    case ADDRESS_ZERO:
    case HEX_ZERO:
      this._value  = nbi();
      break;

    case "1":
    case UInt160.ONE:
    case ADDRESS_ONE:
    case HEX_ONE:
      this._value  = new BigInteger([1]);

      break;

    default:
      if ('string' !== typeof j) {
	this._value  = NaN;
      }
      else if (j[0] === "r") {
	this._value  = Base.decode_check(Base.VER_ACCOUNT_ID, j);
      }
      else if (20 === j.length) {
	this._value  = new BigInteger(utils.stringToArray(j), 256);
      }
      else if (40 === j.length) {
	// XXX Check char set!
	this._value  = new BigInteger(j, 16);
      }
      else {
	this._value  = NaN;
      }
  }

  return this;
};

// value = NaN on error.
UInt160.prototype.parse_json = function (j) {
  // Canonicalize and validate
  if (config.accounts && j in config.accounts)
    j = config.accounts[j].account;

  if ('string' !== typeof j) {
    this._value  = NaN;
  }
  else if (j[0] === "r") {
    this._value  = Base.decode_check(Base.VER_ACCOUNT_ID, j);
  }
  else {
    this._value  = NaN;
  }

  return this;
};

// Convert from internal form.
// XXX Json form should allow 0 and 1, C++ doesn't currently allow it.
UInt160.prototype.to_json = function () {
  if (!(this._value instanceof BigInteger))
    return NaN;

  var bytes   = this._value.toByteArray().map(function (b) { return b ? b < 0 ? 256+b : b : 0});
  var target  = 20;

  // XXX Make sure only trim off leading zeros.
  var array = bytes.length < target
		? bytes.length
		  ? [].concat(utils.arraySet(target - bytes.length, 0), bytes)
		  : utils.arraySet(target, 0)
		: bytes.slice(target - bytes.length);
  var output = Base.encode_check(Base.VER_ACCOUNT_ID, array);

  return output;
};

exports.UInt160 = UInt160;
