
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
// UInt256 support
//

var UInt256 = extend(function () {
  // Internal form: NaN or BigInteger
  this._value  = NaN;
}, UInt);

UInt256.width = 32;
UInt256.prototype = extend({}, UInt.prototype);
UInt256.prototype.constructor = UInt256;

// XXX Generate these constants (or remove them)
var ADDRESS_ZERO = UInt256.ADDRESS_ZERO = "XXX";
var ADDRESS_ONE  = UInt256.ADDRESS_ONE = "XXX";
var HEX_ZERO     = UInt256.HEX_ZERO = "00000000000000000000000000000000" +
                                      "00000000000000000000000000000000";
var HEX_ONE      = UInt256.HEX_ONE  = "00000000000000000000000000000000" +
                                      "00000000000000000000000000000001";
var STR_ZERO     = UInt256.STR_ZERO = utils.hexToString(HEX_ZERO);
var STR_ONE      = UInt256.STR_ONE = utils.hexToString(HEX_ONE);

exports.UInt256 = UInt256;
