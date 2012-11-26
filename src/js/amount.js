// Represent Ripple amounts and currencies.
// - Numbers in hex are big-endian.

var sjcl    = require('./sjcl/core.js');
var bn	    = require('./sjcl/core.js').bn;
var utils   = require('./utils.js');
var jsbn    = require('./jsbn.js');

var BigInteger	= jsbn.BigInteger;
var nbi		= jsbn.nbi;

var alphabets	= {
  'ripple' : "rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz",
  'bitcoin' : "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"
};

var consts = exports.consts = {
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

  for (i = 0; i != input.length && input[i] === alphabet[0]; i += 1)
    ;

  for (; i != input.length; i += 1) {
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
  this._value  = NaN;
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

UInt160.is_valid = function (j) {
  return UInt160.from_json(j).is_valid();
};

UInt160.prototype.clone = function() {
  return this.copyTo(new UInt160());
};

// Returns copy.
UInt160.prototype.copyTo = function(d) {
  d._value = this._value;

  return d;
};

UInt160.prototype.equals = function(d) {
  return isNaN(this._value) || isNaN(d._value) ? false : this._value.equals(d._value);
};

// value = NaN on error.
UInt160.prototype.parse_json = function (j) {
  // Canonicalize and validate
  if (exports.config.accounts && j in exports.config.accounts)
    j = exports.config.accounts[j].account;

  switch (j) {
    case undefined:
    case "0":
    case consts.address_xns:
    case consts.uint160_xns:
    case consts.hex_xns:
      this._value  = nbi();
      break;

    case "1":
    case consts.address_one:
    case consts.uint160_one:
    case consts.hex_one:
      this._value  = new BigInteger([1]);

      break;

    default:
      if ('string' !== typeof j) {
	this._value  = NaN;
      }
      else if (20 === j.length) {
	this._value  = new BigInteger(utils.stringToArray(j), 256);
      }
      else if (40 === j.length) {
	// XXX Check char set!
	this._value  = new BigInteger(j, 16);
      }
      else if (j[0] === "r") {
	this._value  = decode_base_check(0, j);
      }
      else {
	this._value  = NaN;
      }
  }

  return this;
};

// Convert from internal form.
// XXX Json form should allow 0 and 1, C++ doesn't currently allow it.
UInt160.prototype.to_json = function () {
  if (isNaN(this._value))
    return NaN;

  var bytes   = this._value.toByteArray().map(function (b) { return b ? b < 0 ? 256+b : b : 0});
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

UInt160.prototype.is_valid = function () {
  return !isNaN(this._value);
};

// XXX Internal form should be UInt160.
var Currency = function () {
  // Internal form: 0 = XRP. 3 letter-code.
  // XXX Internal should be 0 or hex with three letter annotation when valid.

  // Json form:
  //  '', 'XRP', '0': 0
  //  3-letter code: ...
  // XXX Should support hex, C++ doesn't currently allow it.

  this._value  = NaN;
}

// Given "USD" return the json.
Currency.json_rewrite = function(j) {
  return Currency.from_json(j).to_json();
};

Currency.from_json = function (j) {
  return 'string' === typeof j
      ? (new Currency()).parse_json(j)
      : j.clone();
};

Currency.is_valid = function (j) {
  return currency.from_json(j).is_valid();
};

Currency.prototype.clone = function() {
  return this.copyTo(new Currency());
};

// Returns copy.
Currency.prototype.copyTo = function(d) {
  d._value = this._value;

  return d;
};

Currency.prototype.equals = function(d) {
  return ('string' !== typeof this._value && isNaN(this._value))
    || ('string' !== typeof d._value && isNaN(d._value)) ? false : this._value === d._value;
}

// this._value = NaN on error.
Currency.prototype.parse_json = function(j) {
  if ("" === j || "0" === j || "XRP" === j) {
    this._value	= 0;
  }
  else if ('string' != typeof j || 3 !== j.length) {
    this._value	= NaN;
  }
  else {
    this._value	= j;
  }

  return this;
};

Currency.prototype.is_valid = function () {
  return !isNaN(this._value);
};

Currency.prototype.to_json = function () {
  return this._value ? this._value : "XRP";
};

Currency.prototype.to_human = function() {
  return this._value ? this._value : "XRP";
};

var Amount = function () {
  // Json format:
  //  integer : XRP
  //  { 'value' : ..., 'currency' : ..., 'issuer' : ...}

  this._value	    = new BigInteger();	// NaN for bad value. Always positive for non-XRP.
  this._offset	    = undefined;	// For non-XRP.
  this._is_native   = true;		// Default to XRP. Only valid if value is not NaN.
  this._is_negative = undefined;	// For non-XRP. Undefined for XRP.

  this._currency    = new Currency();
  this._issuer	    = new UInt160();
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

Amount.from_human = function(j) {
  return (new Amount()).parse_human(j);
};

Amount.is_valid = function (j) {
  return Amount.from_json(j).is_valid();
};

Amount.is_valid_full = function (j) {
  return Amount.from_json(j).is_valid_full();
};

Amount.prototype.clone = function(negate) {
  return this.copyTo(new Amount(), negate);
};

// Returns copy.
Amount.prototype.copyTo = function(d, negate) {
  if ('object' === typeof this._value)
  {
    if (this._is_native && negate)
      this._value.negate().copyTo(d._value);
    else
      this._value.copyTo(d._value);
  }
  else
  {
    d._value   = this._value;
  }

  d._offset	  = this._offset;
  d._is_native	  = this._is_native;
  d._is_negative  = this._is_native
		      ? undefined		    // Native sign in BigInteger.
		      : negate
			? !this._is_negative   // Negating.
			: this._is_negative;   // Just copying.

  this._currency.copyTo(d._currency);
  this._issuer.copyTo(d._issuer);

  return d;
};

Amount.prototype.currency = function() {
  return this._currency;
};

Amount.prototype.set_currency = function(c) {
  if ('string' === typeof c) {
    this._currency.parse_json(c);  
  }
  else
  {
    c.copyTo(this._currency);
  }

  return this;
};

// Only checks the value. Not the currency and issuer.
Amount.prototype.is_valid = function() {
  return !isNaN(this._value);
};

Amount.prototype.is_valid_full = function() {
  return this.is_valid() && this._currency.is_valid() && this._issuer.is_valid();
};

Amount.prototype.issuer = function() {
  return this._issuer;
};

Amount.prototype.to_number = function(allow_nan) {
  var s = this.to_text(allow_nan);

  return ('string' === typeof s) ? Number(s) : s;
}

// Convert only value to JSON wire format.
Amount.prototype.to_text = function(allow_nan) {
  if (isNaN(this._value)) {
    // Never should happen.
    return allow_nan ? NaN : "0";
  }
  else if (this._is_native) {
    if (this._value.compareTo(consts.bi_xns_max) > 0 || this._value.compareTo(consts.bi_xns_min) < 0)
    {
      // Never should happen.
      return allow_nan ? NaN : "0";
    }
    else
    {
      return this._value.toString();
    }
  }
  else if (this._value.equals(BigInteger.ZERO))
  {
    return "0";
  }
  else if (this._offset < -25 || this._offset > -5)
  {
    // Use e notation.
    // XXX Clamp output.

    return (this._is_negative ? "-" : "") + this._value.toString() + "e" + this._offset;
  }
  else
  {
    var val = "000000000000000000000000000" + this._value.toString() + "00000000000000000000000";
    var	pre = val.substring(0, this._offset + 43);
    var	post = val.substring(this._offset + 43);
    var	s_pre = pre.match(/[1-9].*$/);	  // Everything but leading zeros.
    var	s_post = post.match(/[1-9]0*$/);  // Last non-zero plus trailing zeros.

    return (this._is_negative ? "-" : "")
      + (s_pre ? s_pre[0] : "0")
      + (s_post ? "." + post.substring(0, 1+post.length-s_post[0].length) : "");
  }
};

/**
 * Format only value in a human-readable format.
 *
 * @example
 *   var pretty = amount.to_human({precision: 2});
 *
 * @param opts Options for formatter.
 * @param opts.precision {Number} Max. number of digits after decimal point.
 */
Amount.prototype.to_human = function (opts)
{
  opts = opts || {};

  var int_part = this._value.divide(consts.bi_xns_unit).toString(10);
  var fraction_part = this._value.mod(consts.bi_xns_unit).toString(10);

  int_part = int_part.replace(/^0*/, '');
  fraction_part = fraction_part.replace(/0*$/, '');

  if ("number" === typeof opts.precision) {
    fraction_part = fraction_part.slice(0, opts.precision);
  }

  var formatted = '';
  formatted += int_part.length ? int_part : '0';
  formatted += fraction_part.length ? '.'+fraction_part : '';

  return formatted;
};

Amount.prototype.canonicalize = function() {
  if (isNaN(this._value) || !this._currency) {
    // nothing
  }
  else if (this._value.equals(BigInteger.ZERO)) {
    this._offset      = -100;
    this._is_negative = false;
  }
  else
  {
    while (this._value.compareTo(consts.bi_man_min_value) < 0) {
      this._value  = this._value.multiply(consts.bi_10);
      this._offset -= 1;
    }

    while (this._value.compareTo(consts.bi_man_max_value) > 0) {
      this._value  = this._value.divide(consts.bi_10);
      this._offset += 1;
    }
  }

  return this;
};

Amount.prototype.is_native = function () {
  return this._is_native;
};

// Return a new value.
Amount.prototype.negate = function () {
  return this.clone('NEGATE');
};

Amount.prototype.to_json = function() {
  if (this._is_native) {
    return this.to_text();
  }
  else
  {
    return {
      'value' : this.to_text(),
      'currency' : this._currency.to_json(),
      'issuer' : this._issuer.to_json(),
    };
  }
};

Amount.prototype.to_text_full = function() {
  return isNaN(this._value)
    ? NaN
    : this._is_native
      ? this.to_text() + "/XRP"
      : this.to_text() + "/" + this._currency.to_json() + "/" + this._issuer.to_json();
};

/**
 * Tries to correctly interpret an amount as entered by a user.
 *
 * Examples:
 *
 *   XRP 250     => 250000000/XRP
 *   25.2 XRP    => 25200000/XRP
 *   USD 100.40  => 100.4/USD/?
 *   100         => 100000000/XRP
 */
Amount.prototype.parse_human = function(j) {
  // Cast to string
  j = ""+j;

  // Parse
  var m = j.match(/^\s*([a-z]{3})?\s*(-)?(\d+)(?:\.(\d*))?\s*([a-z]{3})?\s*$/i);

  if (m) {
    var currency   = m[1] || m[5] || "XRP",
        integer    = m[3] || "0",
        fraction   = m[4] || "",
        precision  = null;

    currency = currency.toUpperCase();

    this._value = new BigInteger(integer);
    this.set_currency(currency);

    // XRP have exactly six digits of precision
    if (currency === 'XRP') {
      fraction = fraction.slice(0, 6);
      while (fraction.length < 6) {
        fraction += "0";
      }
      this._is_native   = true;
      this._value  = this._value.multiply(consts.bi_xns_unit).add(new BigInteger(fraction));
    }
    // Other currencies have arbitrary precision
    else {
      while (fraction[fraction.length - 1] === "0") {
        fraction = fraction.slice(0, fraction.length - 1);
      }

      precision = fraction.length;

      this._is_native   = false;
      var multiplier    = consts.bi_10.clone().pow(precision);
      this._value      	= this._value.multiply(multiplier).add(fraction);
      this._offset     	= -precision;

      this.canonicalize();
    }

    this._is_negative = !!m[2];
  } else {
    this._value	      = NaN;
  }

  return this;
};

// Parse a XRP value from untrusted input.
// - integer = raw units
// - float = with precision 6
// XXX Improvements: disallow leading zeros.
Amount.prototype.parse_native = function(j) {
  var m;

  if ('string' === typeof j)
    m = j.match(/^(-?)(\d+)(\.\d{0,6})?$/);

  if (m) {
    if (undefined === m[3]) {
      // Integer notation

      this._value	  = new BigInteger(m[2]);
    }
    else {
      // Float notation

      var   int_part	  = (new BigInteger(m[2])).multiply(consts.bi_xns_unit);
      var   fraction_part = (new BigInteger(m[3])).multiply(new BigInteger(String(Math.pow(10, 1+consts.xns_precision-m[3].length))));

      this._value	  = int_part.add(fraction_part);
    }

    if (m[1])
      this._value  = this._value.negate();

    this._is_native   = true;
    this._offset      = undefined;
    this._is_negative = undefined;

    if (this._value.compareTo(consts.bi_xns_max) > 0 || this._value.compareTo(consts.bi_xns_min) < 0)
    {
      this._value	  = NaN;
    }
  }
  else {
    this._value	      = NaN;
  }

  return this;
};

// Parse a non-native value for the json wire format.
// Requires _currency to be set!
Amount.prototype.parse_value = function(j) {
  this._is_native    = false;

  if ('number' === typeof j) {
    this._is_negative  = j < 0;
      if (this._is_negative) j = -j;
    this._value	      = new BigInteger(j);
    this._offset      = 0;

    this.canonicalize();
  }
  else if ('string' === typeof j) {
    var	i = j.match(/^(-?)(\d+)$/);
    var	d = !i && j.match(/^(-?)(\d+)\.(\d*)$/);
    var	e = !e && j.match(/^(-?)(\d+)e(\d+)$/);

    if (e) {
      // e notation

      this._value	= new BigInteger(e[2]);
      this._offset 	= parseInt(e[3]);
      this._is_negative	= !!e[1];

      this.canonicalize();
    }
    else if (d) {
      // float notation : values multiplied by 1,000,000.

      var integer	= new BigInteger(d[2]);
      var fraction    	= new BigInteger(d[3]);
      var precision	= d[3].length;

      this._value      	= integer.multiply(consts.bi_10.clone().pow(precision)).add(fraction);
      this._offset     	= -precision;
      this._is_negative = !!d[1];

      this.canonicalize();
    }
    else if (i) {
      // integer notation

      this._value	= new BigInteger(i[2]);
      this._offset 	= 0;
      this._is_negative  = !!i[1];

      this.canonicalize();
    }
    else {
      this._value	= NaN;
    }
  }
  else if (j.constructor == BigInteger) {
    this._value	      = j.clone();
  }
  else {
    this._value	      = NaN;
  }

  return this;
};

// <-> j
Amount.prototype.parse_json = function(j) {
  if ('string' === typeof j) {
    // .../.../... notation is not a wire format.  But allowed for easier testing.
    var	m = j.match(/^(.+)\/(...)\/(.+)$/);

    if (m) {
      this._currency  = Currency.from_json(m[2]);
      this._issuer    = UInt160.from_json(m[3]);
      this.parse_value(m[1]);
    }
    else {
      this.parse_native(j);
      this._currency  = new Currency();
      this._issuer    = new UInt160();
    }
  }
  else if ('object' === typeof j && j.constructor == Amount) {
    j.copyTo(this);
  }
  else if ('object' === typeof j && 'value' in j) {
    // Parse the passed value to sanitize and copy it.

    this._currency.parse_json(j.currency);     // Never XRP.
    this._issuer.parse_json(j.issuer);
    this.parse_value(j.value);
  }
  else {
    this._value	    = NaN;
  }

  return this;
};

Amount.prototype.parse_issuer = function (issuer) {
  this._issuer.parse_json(issuer);

  return this;
};

// Check BigInteger NaN
// Checks currency, does not check issuer.
Amount.prototype.equals = function (d) {
  return 'string' === typeof (d)
    ? this.equals(Amount.from_json(d))
    : this === d
      || (d.constructor === Amount
	&& this._is_native === d._is_native
	&& (this._is_native
	    ? this._value.equals(d._value)
	    : this._currency.equals(d._currency)
	      ? this._is_negative === d._is_negative
		? this._value.equals(d._value)
		: this._value.equals(BigInteger.ZERO) && d._value.equals(BigInteger.ZERO)
	      : false));
};

Amount.prototype.not_equals_why = function (d) {
  return 'string' === typeof (d)
    ? this.not_equals_why(Amount.from_json(d))
    : this === d
      ? false
      : d.constructor === Amount
	  ? this._is_native === d._is_native
	    ? this._is_native
		? this._value.equals(d._value)
		  ? false
		  : "XRP value differs."
		: this._currency.equals(d._currency)
		  ? this._is_negative === d._is_negative
		    ? this._value.equals(d._value)
		      ? false
		      : this._value.equals(BigInteger.ZERO) && d._value.equals(BigInteger.ZERO)
			? false
			: "Non-XRP value differs."
		    : "Non-XRP sign differs."
		  : "Non-XRP currency differs (" + JSON.stringify(this._currency) + "/" + JSON.stringify(d._currency) + ")"
	    : "Native mismatch"
	  : "Wrong constructor."
};

exports.Amount	      = Amount;
exports.Currency      = Currency;
exports.UInt160	      = UInt160;

exports.config	      = {};

// vim:sw=2:sts=2:ts=8:et
