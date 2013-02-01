
var sjcl    = require('../../build/sjcl');
var utils   = require('./utils');
var config  = require('./config');
var jsbn    = require('./jsbn');
var extend  = require('extend');

var BigInteger = jsbn.BigInteger;
var nbi        = jsbn.nbi;

var UInt = require('./uint').UInt,
    Base = require('./base').Base;

//
// UInt160 support
//

var UInt160 = extend(function () {
  // Internal form: NaN or BigInteger
  this._value  = NaN;
}, UInt);

UInt160.width = 20;
UInt160.prototype = extend({}, UInt.prototype);
UInt160.prototype.constructor = UInt160;

var ADDRESS_ZERO = UInt160.ADDRESS_ZERO = "rrrrrrrrrrrrrrrrrrrrrhoLvTp";
var ADDRESS_ONE  = UInt160.ADDRESS_ONE = "rrrrrrrrrrrrrrrrrrrrBZbvji";
var HEX_ZERO     = UInt160.HEX_ZERO = "0000000000000000000000000000000000000000";
var HEX_ONE      = UInt160.HEX_ONE = "0000000000000000000000000000000000000001";
var STR_ZERO     = UInt160.STR_ZERO = utils.hexToString(HEX_ZERO);
var STR_ONE      = UInt160.STR_ONE = utils.hexToString(HEX_ONE);

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

// XXX Json form should allow 0 and 1, C++ doesn't currently allow it.
UInt160.prototype.to_json = function () {
  if (!(this._value instanceof BigInteger))
    return NaN;

  var output = Base.encode_check(Base.VER_ACCOUNT_ID, this.to_bytes());

  return output;
};

exports.UInt160 = UInt160;
