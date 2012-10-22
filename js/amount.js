// Represent Ripple amounts and currencies.
// - Numbers in hex are big-endian.

var sjcl    = require('./sjcl/core.js');
var bn	    = require('./sjcl/core.js').bn;
var utils   = require('./utils.js');
var jsbn    = require('./jsbn.js');

// Don't include in browser context.
var config    = require('../test/config.js');

var BigInteger	= jsbn.BigInteger;
var nbi		= jsbn.nbi;

var alphabets = {
  'ripple' : "rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz",
  'bitcoin' : "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"
};

// --> input: big-endian array of bytes. 
// <-- string at least as long as input.
var encode_base = function (input, alphabet) {
  var alphabet	= alphabets[alphabet || 'ripple'];
  var bi_base	= new BigInteger(String(alphabet.length));
  var bi_q	= nbi();
  var bi_r	= nbi();
  var bi_value	= new BigInteger(input);
  var buffer	= [];

  while (bi_value.compareTo(BigInteger.ZERO) > 0)
  {
    bi_value.divRemTo(bi_base, bi_q, bi_r);
    bi_q.copyTo(bi_value);

    buffer.push(alphabet[bi_r.intValue()]);
  }

  var i;

  for (i = 0; i != input.length && !input[i]; i += 1) {
    buffer.push(alphabet[0]);
  }

  return buffer.reverse().join("");
};

// --> input: String
// <-- array of bytes or undefined.
var decode_base = function (input, alphabet) {
  var alphabet	= alphabets[alphabet || 'ripple'];
  var bi_base	= new BigInteger(String(alphabet.length));
  var bi_value	= nbi();
  var i;

  while (i != input.length && input[i] === alphabet[0])
    i += 1;

  for (i = 0; i != input.length; i += 1) {
    var	v = alphabet.indexOf(input[i]);

    if (v < 0)
      return undefined;

    var r = nbi();

    r.fromInt(v); 

    bi_value  = bi_value.multiply(bi_base).add(r); 
  }

  // toByteArray:
  // - Returns leading zeros!
  // - Returns signed bytes!
  var bytes =  bi_value.toByteArray().map(function (b) { return b ? b < 0 ? 256+b : b : 0});
  var extra = 0;
  
  while (extra != bytes.length && !bytes[extra])
    extra += 1;

  if (extra)
    bytes = bytes.slice(extra);

  var zeros = 0;

  while (zeros !== input.length && input[zeros] === alphabet[0])
    zeros += 1;

  return [].concat(utils.arraySet(zeros, 0), bytes);
};

var sha256  = function (bytes) {
  return sjcl.codec.bytes.fromBits(sjcl.hash.sha256.hash(sjcl.codec.bytes.toBits(bytes)));
};

var sha256hash = function (bytes) {
  return sha256(sha256(bytes));
};

// --> input: Array
// <-- String
var encode_base_check = function (version, input, alphabet) {
  var buffer  = [].concat(version, input);
  var check   = sha256(sha256(buffer)).slice(0, 4);

  return encode_base([].concat(buffer, check), alphabet);
}

// --> input : String
// <-- NaN || BigInteger
var decode_base_check = function (version, input, alphabet) {
  var buffer = decode_base(input, alphabet);

  if (!buffer || buffer[0] !== version || buffer.length < 5)
    return NaN;

  var computed	= sha256hash(buffer.slice(0, -4)).slice(0, 4);
  var checksum	= buffer.slice(-4);
  var i;

  for (i = 0; i != 4; i += 1)
    if (computed[i] !== checksum[i])
      return NaN;

  return new BigInteger(buffer.slice(1, -4));
}

var UInt160 = function () {
  // Internal form: NaN or BigInteger
  this.value  = NaN;
};

UInt160.json_rewrite = function (j) {
  return UInt160.from_json(j).to_json();
};

// Return a new UInt160 from j.
UInt160.from_json = function (j) {
  return 'string' === typeof j
    ? (new UInt160()).parse_json(j)
    : j.clone();
};

UInt160.prototype.clone = function() {
  return this.copyTo(new UInt160());
};

// Returns copy.
UInt160.prototype.copyTo = function(d) {
  d.value = this.value;

  return d;
};

UInt160.prototype.equals = function(d) {
  return isNaN(this.value) || isNaN(d.value) ? false : this.value.equals(d.value);
};

// value = NaN on error.
UInt160.prototype.parse_json = function (j) {
  // Canonicalize and validate
  if (config.accounts && j in config.accounts)
    j = config.accounts[j].account;

  switch (j) {
    case undefined:
    case "0":
    case exports.consts.address_xns:
    case exports.consts.uint160_xns:
    case exports.consts.hex_xns:
      this.value  = nbi();
      break;

    case "1":
    case exports.consts.address_one:
    case exports.consts.uint160_one:
    case exports.consts.hex_one:
      this.value  = new BigInteger([1]);

      break;

    default:
      if ('string' !== typeof j) {
	this.value  = NaN;
      }
      else if (20 === j.length) {
	this.value  = new BigInteger(utils.stringToArray(j), 256);
      }
      else if (40 === j.length) {
	// XXX Check char set!
	this.value  = new BigInteger(j, 16);
      }
      else if (j[0] === "r") {
	this.value  = decode_base_check(0, j);
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
  if (isNaN(this.value))
    return NaN;
  
  var bytes   = this.value.toByteArray().map(function (b) { return b ? b < 0 ? 256+b : b : 0});
  var target  = 20;

  // XXX Make sure only trim off leading zeros.
  var array = bytes.length < target
		? bytes.length
		  ? [].concat(utils.arraySet(target - bytes.length, 0), bytes)
		  : utils.arraySet(target, 0)
		: bytes.slice(target - bytes.length);
  var output = encode_base_check(0, array);

  return output;
};

var Currency = function () {
  // Internal form: 0 = XNS. 3 letter-code.
  // XXX Internal should be 0 or hex with three letter annotation when valid.

  // Json form:
  //  '', 'XNS', '0': 0
  //  3-letter code: ...
  // XXX Should support hex, C++ doesn't currently allow it.

  this.value  = NaN;
}

Currency.from_json = function (j) {
  return (new Currency()).parse_json(j);
};

Currency.prototype.clone = function() {
  return this.copyTo(new Currency());
};

// Returns copy.
Currency.prototype.copyTo = function(d) {
  d.value = this.value;

  return d;
};

// this.value = NaN on error.
Currency.prototype.parse_json = function(j) {
  if ("" === j || "0" === j || "XNS" === j) {
    this.value	= 0;
  }
  else if ('string' != typeof j || 3 !== j.length) {
    this.value	= NaN;
  }
  else {
    this.value	= j;
  }

  return this;
};

Currency.prototype.to_json = function () {
  return this.value ? this.value : "XNS";
};

Currency.prototype.to_human = function() {
  return this.value ? this.value : "XNS";
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

// Given "100/USD/mtgox" return the a string with mtgox remapped.
Amount.text_full_rewrite = function (j) {
  return Amount.from_json(j).to_text_full();
}

// Given "100/USD/mtgox" return the json.
Amount.json_rewrite = function(j) {
  return Amount.from_json(j).to_json();
};

Amount.from_json = function(j) {
  return (new Amount()).parse_json(j);
};

Amount.prototype.clone = function(negate) {
  return this.copyTo(new Amount(), negate);
};

// Returns copy.
Amount.prototype.copyTo = function(d, negate) {
  if ('object' === typeof this.value)
  {
    if (this.is_native && negate)
      this.value.negate().copyTo(d.value);
    else
      this.value.copyTo(d.value);
  }
  else
  {
    d.value   = this.value;
  }

  d.offset	= this.offset;
  d.is_native	= this.is_native;
  d.is_negative	= this.is_native ? undefined : !this.is_negative;

  this.currency.copyTo(d.currency);
  this.issuer.copyTo(d.issuer);

  return d;
};

// YYY Might also provide is_valid_json.
Amount.prototype.is_valid = function() {
  return !isNaN(this.value);
};

// Convert only value to JSON wire format.
Amount.prototype.to_text = function(allow_nan) {
  if (isNaN(this.value)) {
    // Never should happen.
    return allow_nan ? NaN : "0";
  }
  else if (this.is_native) {
    if (this.value.compareTo(exports.consts.bi_xns_max) > 0 || this.value.compareTo(exports.consts.bi_xns_min) < 0)
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
  else if (this.offset < -25 || this.offset > -5)
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
    var	s_pre = pre.match(/[1-9].*$/);	  // Everything but leading zeros.
    var	s_post = post.match(/[1-9]0*$/);  // Last non-zero plus trailing zeros.

    return (this.is_negative ? "-" : "")
      + (s_pre ? s_pre[0] : "0")
      + (s_post ? "." + post.substring(0, 1+post.length-s_post[0].length) : "");
  }
};

Amount.prototype.canonicalize = function() {
  if (isNaN(this.value) || !this.currency) {
    // nothing
  }
  else if (this.value.equals(BigInteger.ZERO)) {
    this.offset	      = -100;
    this.is_negative  = false;
  }
  else
  {
    while (this.value.compareTo(exports.consts.bi_man_min_value) < 0) {
      this.value  = this.value.multiply(exports.consts.bi_10);
      this.offset -= 1;
    }

    while (this.value.compareTo(exports.consts.bi_man_max_value) > 0) {
      this.value  = this.value.divide(exports.consts.bi_10);
      this.offset += 1;
    }
  }

  return this;
};

// Return a new value.
Amount.prototype.negate = function () {
  return this.clone('NEGATE');
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

Amount.prototype.to_text_full = function() {
  return isNaN(this.value)
    ? NaN
    : this.is_native
      ? this.to_text() + "/XNS"
      : this.to_text() + "/" + this.currency.to_json() + "/" + this.issuer.to_json();
};

// Parse a XNS value from untrusted input.
// - integer = raw units
// - float = with precision 6
// XXX Improvements: disallow leading zeros.
Amount.prototype.parse_native = function(j) {
  var m;

  if ('string' === typeof j)
    m = j.match(/^(-?)(\d+)(\.\d{1,6})?$/);

  if (m) {
    if (undefined === m[3]) {
      // Integer notation

      this.value	  = new BigInteger(m[2]);
    }
    else {
      // Decimal notation

      var   int_part	  = (new BigInteger(m[2])).multiply(exports.consts.bi_xns_unit);
      var   fraction_part = (new BigInteger(m[3])).multiply(new BigInteger(String(Math.pow(10, 1+exports.consts.xns_precision-m[3].length))));

      this.value	  = int_part.add(fraction_part);
    }

    if (m[1])
      this.value  = this.value.negate();

    this.is_native    = true;
    this.offset	      = undefined;
    this.is_negative  = undefined;

    if (this.value.compareTo(exports.consts.bi_xns_max) > 0 || this.value.compareTo(exports.consts.bi_xns_min) < 0)
    {
      this.value	  = NaN;
    }
  } 
  else {
    this.value	      = NaN;
  }

  return this;
};

// Parse a non-native value.
Amount.prototype.parse_value = function(j) {
  this.is_native    = false;

  if ('number' === typeof j) {
    this.is_negative  = j < 0;
      if (this.is_negative) j = -j;
    this.value	      = new BigInteger(j);
    this.offset	      = 0;

    this.canonicalize();
  } 
  else if ('string' === typeof j) {
    var	i = j.match(/^(-?)(\d+)$/);
    var	d = !i && j.match(/^(-?)(\d+)\.(\d*)$/);
    var	e = !e && j.match(/^(-?)(\d+)e(\d+)$/);

    if (e) {
      // e notation
    
      this.value	= new BigInteger(e[2]);
      this.offset 	= parseInt(e[3]);
      this.is_negative  = !!e[1];

      this.canonicalize();
    }
    else if (d) {
      // float notation

      var integer	= new BigInteger(d[2]);
      var fraction    	= new BigInteger(d[3]);
      var precision	= d[3].length;

      this.value      	= integer.multiply(exports.consts.bi_10.clone().pow(precision)).add(fraction);
      this.offset     	= -precision;
      this.is_negative  = !!d[1];

      this.canonicalize();
    }
    else if (i) {
      // integer notation

      this.value	= new BigInteger(i[2]);
      this.offset 	= 0;
      this.is_negative  = !!i[1];

      this.canonicalize();
    }
    else {
      this.value	= NaN;
    }
  }
  else if (j.constructor == BigInteger) {
    this.value	      = j.clone();
  }
  else {
    this.value	      = NaN;
  }

  return this;
};

// <-> j
Amount.prototype.parse_json = function(j) {
  if ('string' === typeof j) {
    // .../.../... notation is not a wire format.  But allowed for easier testing.
    var	m = j.match(/^(.+)\/(...)\/(.+)$/);

    if (m) {
      this.parse_value(m[1]);
      this.currency = Currency.from_json(m[2]);
      this.issuer   = UInt160.from_json(m[3]);
    }
    else {
      this.parse_native(j);
      this.currency = new Currency();
      this.issuer   = new UInt160();
    }
  }
  else if ('object' === typeof j && j.constructor == Amount) {
    j.copyTo(this);
  }
  else if ('object' === typeof j && 'value' in j) {
    // Parse the passed value to sanitize and copy it.

    this.parse_value(j.value);
    this.currency.parse_json(j.currency);     // Never XNS.
    this.issuer.parse_json(j.issuer);
  }
  else {
    this.value	    = NaN;
  }

  return this;
};

Amount.prototype.parse_issuer = function (issuer) {
  this.issuer.parse_json(issuer);

  return this;
};

// Check BigInteger NaN
Amount.prototype.equals = function (d) {
  return 'string' === typeof (d)
    ? this.equals(Amount.from_json(d))
    : this === d
      || (d.constructor === Amount
	&& this.is_native === d.is_native
	&& (this.is_native
	    ? this.value.equals(d.value)
	    : this.is_negative === d.is_negative
	      ? this.value.equals(d.value)
	      : this.value.equals(BigInteger.ZERO) && d.value.equals(BigInteger.ZERO)));
};

exports.Amount	      = Amount;
exports.Currency      = Currency;
exports.UInt160	      = UInt160;

exports.consts	  = {
  'address_xns'	      : "rrrrrrrrrrrrrrrrrrrrrhoLvTp",
  'address_one'	      : "rrrrrrrrrrrrrrrrrrrrBZbvji",
  'currency_xns'      : 0,
  'currency_one'      : 1,
  'uint160_xns'	      : utils.hexToString("0000000000000000000000000000000000000000"),
  'uint160_one'	      : utils.hexToString("0000000000000000000000000000000000000001"),
  'hex_xns'	      : "0000000000000000000000000000000000000000",
  'hex_one'	      : "0000000000000000000000000000000000000001",
  'xns_precision'     : 6,

  // BigInteger values prefixed with bi_.
  'bi_10'	      : new BigInteger('10'),
  'bi_man_max_value'  : new BigInteger('9999999999999999'),
  'bi_man_min_value'  : new BigInteger('1000000000000000'),
  'bi_xns_max'	      : new BigInteger("9000000000000000000"),	  // Json wire limit.
  'bi_xns_min'	      : new BigInteger("-9000000000000000000"),	  // Json wire limit.
  'bi_xns_unit'	      : new BigInteger('1000000'),
};

// vim:sw=2:sts=2:ts=8
