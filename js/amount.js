// Represent Newcoin amounts and currencies.


var utils  = require("./utils.js");

var UInt160 = function () {
  // Internal form:
  //   0, 1, 'iXXXXX', 20 byte string, or NaN.
  //   XXX Should standardize on 'i' format or 20 format.
};

// Returns NaN on error.
UInt160.method('parse_json', function (j) {
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

  return this.value;
});

// Convert from internal form.
// XXX Json form should allow 0 and 1, C++ doesn't currently allow it.
UInt160.method('to_json', function () {
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
});

var Currency = function () {
  // Internal form: 0 = XNS. 3 letter-code.
  // XXX Internal should be 0 or hex.

  // Json form:
  //  '', 'XNS', '0': 0
  //  3-letter code: ...
  // XXX Should support hex, C++ doesn't currently allow it.
}

// Returns NaN on error.
Currency.method('parse_json', function (j) {
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
});

Currency.method('to_json', function () {
  return this.value ? this.value : 'XNS';
});

Currency.method('to_human', function() {
  return this.value ? this.value : 'XNS';
});

var Amount = function () {
  // Json format:
  //  integer : XNS
  //  { 'value' : ..., 'currency' : ..., 'issuer' : ...}

  this.value	    = 0;
  this.offset	    = 0;
  this.is_native    = false;
  this.is_negative  = false;

  this.currency	    = new Currency();
  this.issuer	    = new UInt160();

}

// Convert only value to JSON text.
Amount.method('to_text', function() {
  // XXX Needs to work for native and non-native.
  return this.is_negative ? -this.value : this.value;	  // XXX Use biginteger.
});

Amount.method('to_json', function() {
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
});

// Parse a native value.
Amount.method('parse_native', function() {
  if ('integer' === typeof j) {
    // XNS
    this.value	      = x >= 0 ? j : -j;  // XXX Use biginteger.
    this.offset	      = 0;
    this.is_native    = true;
    this.is_negative  = x < 0;
  } 
  else if ('string' === typeof j) {
    this.value	      = x >= 0 ? j : -j;  // XXX Use biginteger.
    this.offset	      = 0;
    this.is_native    = true;
    this.is_negative  = x < 0;
  }
  else {
    this.value	      = NaN;
  }
});

// Parse a non-native value.
Amount.method('parse_value', function() {
  if ('integer' === typeof j) {
    this.value	      = x >= 0 ? j : -j;  // XXX Use biginteger.
    this.offset	      = 0;
    this.is_native    = false;
    this.is_negative  = x < 0;
  } 
  else if ('string' === typeof j) {
    this.value	      = x >= 0 ? j : -j;  // XXX Use biginteger.
    this.offset	      = 0;
    this.is_native    = false;
    this.is_negative  = x < 0;
  }
  else {
    this.value	      = NaN;
  }
});

// <-> j
Amount.method('parse_json', function(j) {
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
});

exports.UInt160  = UInt160;
exports.Currency  = Currency;
exports.Amount  = Amount;

exports.consts	= {
  'address_xns' : "iiiiiiiiiiiiiiiiiiiiihoLvTp",
  'address_one' : "iiiiiiiiiiiiiiiiiiiiBZbvjr",
  'currency_xns' : 0,
  'currency_one' : 1,
  'uint160_xns' : hexToString("0000000000000000000000000000000000000000"),
  'uint160_one' : hexToString("0000000000000000000000000000000000000001"),
  'hex_xns' : "0000000000000000000000000000000000000000",
  'hex_one' : "0000000000000000000000000000000000000001",
};

// vim:sw=2:sts=2:ts=8
