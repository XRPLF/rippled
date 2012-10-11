// Represent Newcoin amounts and currencies.
// - Numbers in hex are big-endian.

var utils   = require('./utils.js');
var jsbn    = require('./jsbn.js');

var BigInteger	= jsbn.BigInteger;

var UInt160 = function () {
  // Internal form:
  //   0, 1, 'iXXXXX', 20 byte string, or NaN.
  //   XXX Should standardize on 'i' format or 20 format.
};

UInt160.from_json = function (j) {
  var u	= new UInt160();

  return u.parse_json(j);
};

// value === NaN on error.
UInt160.prototype.parse_json = function (j) {
  // Canonicalize and validate

  switch (j) {
    case undefined:
    case "0":
    case exports.consts.address_xns:
    case exports.consts.uint160_xns:
    case exports.consts.hex_xns:
      this.value  = 0;
      break;

    case "1":
    case exports.consts.address_one:
    case exports.consts.uint160_one:
    case exports.consts.hex_one:
      this.value  = 1;
      break;

    default:
      if ('string' !== typeof j) {
	this.value  = NaN;
      }
      else if (20 === j.length) {
	this.value  = j;
      }
      else if (40 === j.length) {
	this.value  = utils.hexToString(j);
      }
      else if (j[0] === "i") {
	// XXX Do more checking convert to string.

	this.value  = j;
      }
      else {
	this.value  = NaN;
      }
  }

  return this;
};

// Convert from internal form.
// XXX Json form should allow 0 and 1, C++ doesn't currently allow it.
UInt160.prototype.to_json = function () {
  if ("0" === this.value) {
    return exports.consts.hex_xns;
  }
  else if ("1" === this.value)
  {
    return exports.consts.hex_one;
  }
  else if (20 === this.value.length) {
    return utils.stringToHex(this.value);
  }
  else
  {
    return this.value;  
  }
};

var Currency = function () {
  // Internal form: 0 = XNS. 3 letter-code.
  // XXX Internal should be 0 or hex.

  // Json form:
  //  '', 'XNS', '0': 0
  //  3-letter code: ...
  // XXX Should support hex, C++ doesn't currently allow it.
}

// Returns NaN on error.
Currency.prototype.parse_json = function (j) {
  if ("" === j || "0" === j || "XNS" === j) {
    this.value	= 0;
  }
  else if ('string' != typeof j || 3 !== j.length) {
    this.value	= NaN;
  }
  else {
    this.value	= j;
  }

  return this.value;
};

Currency.prototype.to_json = function () {
  return this.value ? this.value : 'XNS';
};

Currency.prototype.to_human = function() {
  return this.value ? this.value : 'XNS';
};

var Amount = function () {
  // Json format:
  //  integer : XNS
  //  { 'value' : ..., 'currency' : ..., 'issuer' : ...}

  this.value	    = new BigInteger();	// NaN for bad value. Always positive for non-XNS.
  this.offset	    = undefined;	// For non-XNS.
  this.is_native    = true;		// Default to XNS. Only valid if value is not NaN.
  this.is_negative  = undefined;	// Undefined for XNS.

  this.currency	    = new Currency();
  this.issuer	    = new UInt160();
};

// YYY Might also check range.
Amount.prototype.is_valid = function() {
  return NaN !== this.value;
}

// Convert only value to JSON wire format.
Amount.prototype.to_text = function(allow_nan) {
  if (NaN === this.value) {
    // Never should happen.
    return allow_nan ? NaN : "0";
  }
  else if (this.is_native) {
    if (this.value.compareTo(exports.consts.xns_max) > 0 || this.value.compareTo(exports.consts.xns_min) < 0)
    {
      // Never should happen.
      return allow_nan ? NaN : "0";
    }
    else
    {
      return this.value.toString();
    }
  }
  else if (this.value.equals(BigInteger.ZERO))
  {
    return "0"; 
  }
  else if (this.offset < -25 || mOffset > -5)
  {
    // Use e notation.
    // XXX Clamp output.

    return (this.is_negative ? "-" : "") + this.value.toString() + "e" + this.offset;
  }
  else
  {
    var val = "000000000000000000000000000" + this.value.toString() + "00000000000000000000000";
    var	pre = val.substring(0, this.offset + 43);
    var	post = val.substring(this.offset + 43);
    var	s_pre = val.match(/[1-9].*$/);	// Everything but leading zeros.
    var	s_post = val.match(/0+$/);	// Trailing zeros.


    return (this.is_negative ? "-" : "")
      + (null == s_pre ? "0" : s_pre[0])
      + "."
      + post.substring(post.length - s_post.length);
  }
};

Amount.prototype.canonicalize = function() {
  if (NaN === this.value || !this.currency) {
    // nothing
  }
  else if (this.value.equals(BigInteger.ZERO)) {
    this.offset	      = -100;
    this.is_negative  = false;
  }
  else
  {
    while (this.value.compareTo(exports.consts.bi_man_min_value)) {
      this.value.multiply(exports.consts.bi_10);
      this.offset -= 1;
    }

    while (this.value.compareTo(exports.consts.bi_man_max_value)) {
      this.value.divide(exports.consts.bi_10);
      this.offset += 1;
    }
  }
};

Amount.prototype.to_json = function() {
  if (this.is_native) {
    return this.to_text();
  }
  else
  {
    return {
      'value' : this.to_text(),
      'currency' : this.currency.to_json(),
      'issuer' : this.issuer.to_json(),
    };
  }
};

// Parse a XNS value from untrusted input.
// XXX Improvements: disallow leading zeros.
Amount.prototype.parse_native = function(j) {
  var m;

  if ('string' === typeof j)
    m = j.match(/^(\d+)(\.\d{1,6})?$/);

  if ('integer' === typeof j || null !== m) {
    if ('integer' === typeof j || ("" === e[2])) {
      this.value	      = new BigInteger(j);
    }
    else
    {
      // Decimal notation
      var   int_part	  = (new BigInteger(e[1])).multiply(exports.consts.xns_unit);
      var   fraction_part = (new BigInteger(e[2])).multiply(new BigInteger(Math.pow(10, exports.consts.xns_unit-e[2].length)));

      this.value  = int_part.add(fraction_part);
    }
    this.is_native    = true;
    this.offset	      = undefined;
    this.is_negative  = undefined;

    if (this.value.compareTo(exports.consts.xns_max) > 0 || this.value.compareTo(exports.consts.xns_min) < 0)
    {
      this.value	  = NaN;
    }
  } 
  else {
    this.value	      = NaN;
  }
};

// Parse a non-native value.
Amount.prototype.parse_value = function(j) {
  if ('integer' === typeof j) {
    this.value	      = new BigInteger(j);
    this.offset	      = 0;
    this.is_native    = false;
    this.is_negative  = j < 0;

    this.canonicalize();
  } 
  else if ('string' === typeof j) {
    var	e = j.match(/^(-?\d+)e(\d+)/);
    var	d = j.match(/^(-?\d+)\.(\d+)/);

    if (null !== e) {
      // e notation
    
      this.value  = new BigInteger(e[1]);
      this.offset = parseInt(e[2]);
    }
    else if (null !== d) {
      // float notation

      this.value  = (new BigInteger(e[1])).multiply((new BigInteger(exports.consts.bi_10)).pow(e[2].length)).add(new BigInteger(e[2]));
      this.offset = -e[2].length;
    }
    else
    {
      // integer notation

      this.value  = new BigInteger(j);
      this.offset = 0;
    }

    this.is_native    = false;
    this.is_negative  = undefined;

    this.canonicalize();
  }
  else {
    this.value	      = NaN;
  }
};

// <-> j
Amount.prototype.parse_json = function(j) {
  if ('object' === typeof j && j.currency) {

    this.parse_value(j);
    this.currency.parse_json(j.currency);
    this.issuer.parse_json(j.issuer);
  }
  else {
    this.parse_native(j);
    this.currency = 0;
    this.issuer  = 0;
  }
};

exports.Amount	  = Amount;
exports.Currency  = Currency;
exports.UInt160	  = UInt160;

exports.consts	  = {
  'address_xns' : "iiiiiiiiiiiiiiiiiiiiihoLvTp",
  'address_one' : "iiiiiiiiiiiiiiiiiiiiBZbvjr",
  'currency_xns' : 0,
  'currency_one' : 1,
  'uint160_xns' : utils.hexToString("0000000000000000000000000000000000000000"),
  'uint160_one' : utils.hexToString("0000000000000000000000000000000000000001"),
  'hex_xns' : "0000000000000000000000000000000000000000",
  'hex_one' : "0000000000000000000000000000000000000001",
  'xns_max' : new BigInteger("9000000000000000000"),	  // Json wire limit.
  'xns_min' : new BigInteger("-9000000000000000000"),	  // Json wire limit.
  'xns_unit' : new BigInteger('1000000'),
  'bi_man_min_value' : new BigInteger('1000000000000000'),
  'bi_man_max_value' : new BigInteger('9999999999999999'),
  'bi_10' : new BigInteger('10'),
};

// vim:sw=2:sts=2:ts=8
