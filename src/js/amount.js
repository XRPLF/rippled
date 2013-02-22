// Represent Ripple amounts and currencies.
// - Numbers in hex are big-endian.

var sjcl    = require('../../build/sjcl');
var bn	    = sjcl.bn;
var utils   = require('./utils');
var jsbn    = require('./jsbn');

var BigInteger = jsbn.BigInteger;

var UInt160  = require('./uint160').UInt160,
    Seed     = require('./seed').Seed,
    Currency = require('./currency').Currency;

var consts = exports.consts = {
  'currency_xns'          : 0,
  'currency_one'          : 1,
  'xns_precision'         : 6,

  // BigInteger values prefixed with bi_.
  'bi_5'	          : new BigInteger('5'),
  'bi_7'	          : new BigInteger('7'),
  'bi_10'	          : new BigInteger('10'),
  'bi_1e14'               : new BigInteger(String(1e14)),
  'bi_1e16'               : new BigInteger(String(1e16)),
  'bi_1e17'               : new BigInteger(String(1e17)),
  'bi_1e32'               : new BigInteger('100000000000000000000000000000000'),
  'bi_man_max_value'      : new BigInteger('9999999999999999'),
  'bi_man_min_value'      : new BigInteger('1000000000000000'),
  'bi_xns_max'	          : new BigInteger("9000000000000000000"),	  // Json wire limit.
  'bi_xns_min'	          : new BigInteger("-9000000000000000000"),	  // Json wire limit.
  'bi_xns_unit'	          : new BigInteger('1000000'),

  'cMinOffset'            : -96,
  'cMaxOffset'            : 80,
};


//
// Amount class in the style of Java's BigInteger class
// http://docs.oracle.com/javase/1.3/docs/api/java/math/BigInteger.html
//

var Amount = function () {
  // Json format:
  //  integer : XRP
  //  { 'value' : ..., 'currency' : ..., 'issuer' : ...}

  this._value	    = new BigInteger();	// NaN for bad value. Always positive for non-XRP.
  this._offset	    = 0;	        // Always 0 for XRP.
  this._is_native   = true;		// Default to XRP. Only valid if value is not NaN.
  this._is_negative = false;

  this._currency    = new Currency();
  this._issuer	    = new UInt160();
};

// Given "100/USD/mtgox" return the a string with mtgox remapped.
Amount.text_full_rewrite = function (j) {
  return Amount.from_json(j).to_text_full();
}

// Given "100/USD/mtgox" return the json.
Amount.json_rewrite = function (j) {
  return Amount.from_json(j).to_json();
};

Amount.from_json = function (j) {
  return (new Amount()).parse_json(j);
};

Amount.from_human = function (j) {
  return (new Amount()).parse_human(j);
};

Amount.is_valid = function (j) {
  return Amount.from_json(j).is_valid();
};

Amount.is_valid_full = function (j) {
  return Amount.from_json(j).is_valid_full();
};

Amount.NaN = function () {
  var result = new Amount();

  result._value = NaN;

  return result;
};

// Returns a new value which is the absolute value of this.
Amount.prototype.abs = function () {
  return this.clone(this.is_negative());
};

// Result in terms of this' currency and issuer.
Amount.prototype.add = function (v) {
  var result;

  if (!this.is_comparable(v)) {
    result              = Amount.NaN();
  }
  else if (this._is_native) {
    result              = new Amount();

    var v1  = this._is_negative ? this._value.negate() : this._value;
    var v2  = v._is_negative ? v._value.negate() : v._value;
    var s   = v1.add(v2);

    result._is_negative = s.compareTo(BigInteger.ZERO) < 0;
    result._value       = result._is_negative ? s.negate() : s;
  }
  else if (v.is_zero()) {
    result              = this; 
  }
  else if (this.is_zero()) {
    result              = v.clone();
    // YYY Why are these cloned? We never modify them.
    result._currency    = this._currency.clone();
    result._issuer      = this._issuer.clone();
  }
  else
  {
    var v1  = this._is_negative ? this._value.negate() : this._value;
    var o1  = this._offset;
    var v2  = v._is_negative ? v._value.negate() : v._value;
    var o2  = v._offset;

    while (o1 < o2) {
      v1  = v1.divide(consts.bi_10);
      o1  += 1;
    }

    while (o2 < o1) {
      v2  = v2.divide(consts.bi_10);
      o2  += 1;
    }

    result              = new Amount();
    result._is_native   = false;
    result._offset      = o1;
    result._value       = v1.add(v2);
    result._is_negative = result._value.compareTo(BigInteger.ZERO) < 0;

    if (result._is_negative) {
      result._value       = result._value.negate();
    }

    result._currency    = this._currency.clone();
    result._issuer      = this._issuer.clone();

    result.canonicalize();
  }

  return result;
};

Amount.prototype.canonicalize = function () {
  if (!(this._value instanceof BigInteger))
  {
    // NaN.
    // nothing
  }
  else if (this._is_native) {
    // Native.

    if (this._value.equals(BigInteger.ZERO)) {
      this._offset      = 0;
      this._is_negative = false;
    }
    else {
      // Normalize _offset to 0.

      while (this._offset < 0) {
        this._value  = this._value.divide(consts.bi_10);
        this._offset += 1;
      }

      while (this._offset > 0) {
        this._value  = this._value.multiply(consts.bi_10);
        this._offset -= 1;
      }
    }

    // XXX Make sure not bigger than supported. Throw if so.
  }
  else if (this.is_zero()) {
    this._offset      = -100;
    this._is_negative = false;
  }
  else
  {
    // Normalize mantissa to valid range.

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

Amount.prototype.clone = function (negate) {
  return this.copyTo(new Amount(), negate);
};

Amount.prototype.compareTo = function (v) {
  var result;

  if (!this.is_comparable(v)) {
    result  = Amount.NaN();
  }
  else if (this._is_native) {
    result  = this._value.compareTo(v._value);

    if (result > 1)
      result  = 1;
    else if (result < -1)
      result  = -1;
  }
  else if (this._is_negative !== v._is_negative) {
    result  = this._is_negative ? -1 : 1;
  }
  else if (this._value.equals(BigInteger.ZERO)) {
    result  = v._is_negative
                ? 1
                : v._value.equals(BigInteger.ZERO)
                  ? 0
                  : 1;
  }
  else if (v._value.equals(BigInteger.ZERO)) {
    result  = 1;
  }
  else if (this._offset > v._offset) {
    result  = this._is_negative ? -1 : 1;
  }
  else if (this._offset < v._offset) {
    result  = this._is_negative ? 1 : -1;
  }
  else {
    result  = this._value.compareTo(v._value);

    if (result > 1)
      result  = 1;
    else if (result < -1)
      result  = -1;
  }

  return result;
};

// Returns copy.
Amount.prototype.copyTo = function (d, negate) {
  if ('object' === typeof this._value)
  {
    this._value.copyTo(d._value);
  }
  else
  {
    d._value   = this._value;
  }

  d._offset	  = this._offset;
  d._is_native	  = this._is_native;
  d._is_negative  = negate
			? !this._is_negative    // Negating.
			: this._is_negative;    // Just copying.

  this._currency.copyTo(d._currency);
  this._issuer.copyTo(d._issuer);

  return d;
};

Amount.prototype.currency = function () {
  return this._currency;
};

// Check BigInteger NaN
// Checks currency, does not check issuer.
Amount.prototype.equals = function (d) {
  return 'string' === typeof (d)
    ? this.equals(Amount.from_json(d))
    : this === d
      || (d instanceof Amount
	&& this._is_native === d._is_native
	&& (this._is_native
	    ? this._value.equals(d._value)
	    : this._currency.equals(d._currency)
	      ? this._is_negative === d._is_negative
		? this._value.equals(d._value)
		: this._value.equals(BigInteger.ZERO) && d._value.equals(BigInteger.ZERO)
	      : false));
};

// Result in terms of this' currency and issuer.
Amount.prototype.divide = function (d) {
  var result;

  if (d.is_zero()) {
    throw "divide by zero";
  }
  else if (this.is_zero()) {
    result = this.clone();
  }
  else if (!this.is_valid()) {
    throw new Error("Invalid dividend");
  }
  else if (!d.is_valid()) {
    throw new Error("Invalid divisor");
  }
  else {
    result              = new Amount();
    result._offset      = this._offset - d._offset - 17;
    result._value       = this._value.multiply(consts.bi_1e17).divide(d._value).add(consts.bi_5);
    result._is_native   = this._is_native;
    result._is_negative = this._is_negative !== d._is_negative;
    result._currency    = this._currency.clone();
    result._issuer      = this._issuer.clone();

    result.canonicalize();
  }

  return result;
};

/**
 * Calculate a ratio between two amounts.
 *
 * This function calculates a ratio - such as a price - between two Amount
 * objects.
 *
 * The return value will have the same type (currency) as the numerator. This is
 * a simplification, which should be sane in most cases. For example, a USD/XRP
 * price would be rendered as USD.
 *
 * @example
 *   var price = buy_amount.ratio_human(sell_amount);
 *
 * @this {Amount} The numerator (top half) of the fraction.
 * @param {Amount} denominator The denominator (bottom half) of the fraction.
 * @return {Amount} The resulting ratio. Unit will be the same as numerator.
 */
Amount.prototype.ratio_human = function (denominator) {
  if ("number" === typeof denominator && parseInt(denominator) === denominator) {
    // Special handling of integer arguments
    denominator = Amount.from_json("" + denominator + ".0");
  } else {
    denominator = Amount.from_json(denominator);
  }

  var numerator = this;
  denominator = Amount.from_json(denominator);

  // Special case: The denominator is a native (XRP) amount.
  //
  // In that case, it's going to be expressed as base units (1 XRP =
  // 10^xns_precision base units).
  //
  // However, the unit of the denominator is lost, so when the resulting ratio
  // is printed, the ratio is going to be too small by a factor of
  // 10^xns_precision.
  //
  // To compensate, we multiply the numerator by 10^xns_precision.
  if (denominator._is_native) {
    numerator = numerator.clone();
    numerator._value = numerator._value.multiply(consts.bi_xns_unit);
    numerator.canonicalize();
  }

  return numerator.divide(denominator);
};

/**
 * Calculate a product of two amounts.
 *
 * This function allows you to calculate a product between two amounts which
 * retains XRPs human/external interpretation (i.e. 1 XRP = 1,000,000 base
 * units).
 *
 * Intended use is to calculate something like: 10 USD * 10 XRP/USD = 100 XRP
 *
 * @example
 *   var sell_amount = buy_amount.product_human(price);
 *
 * @see Amount#ratio_human
 *
 * @this {Amount} The first factor of the product.
 * @param {Amount} factor The second factor of the product.
 * @return {Amount} The product. Unit will be the same as the first factor.
 */
Amount.prototype.product_human = function (factor) {
  if ("number" === typeof factor && parseInt(factor) === factor) {
    // Special handling of integer arguments
    factor = Amount.from_json("" + factor + ".0");
  } else {
    factor = Amount.from_json(factor);
  }

  var product = this.multiply(factor);

  // Special case: The second factor is a native (XRP) amount expressed as base
  // units (1 XRP = 10^xns_precision base units).
  //
  // See also Amount#ratio_human.
  if (factor._is_native) {
    product._value = product._value.divide(consts.bi_xns_unit);
    product.canonicalize();
  }

  return product;
}

// True if Amounts are valid and both native or non-native.
Amount.prototype.is_comparable = function (v) {
  return this._value instanceof BigInteger
    && v._value instanceof BigInteger
    && this._is_native === v._is_native;
};

Amount.prototype.is_native = function () {
  return this._is_native;
};

Amount.prototype.is_negative = function () {
  return this._value instanceof BigInteger
          ? this._is_negative
          : false;                          // NaN is not negative
};

Amount.prototype.is_positive = function () {
  return !this.is_zero() && !this.is_negative();
};

// Only checks the value. Not the currency and issuer.
Amount.prototype.is_valid = function () {
  return this._value instanceof BigInteger;
};

Amount.prototype.is_valid_full = function () {
  return this.is_valid() && this._currency.is_valid() && this._issuer.is_valid();
};

Amount.prototype.is_zero = function () {
  return this._value instanceof BigInteger
          ? this._value.equals(BigInteger.ZERO)
          : false;
};

Amount.prototype.issuer = function () {
  return this._issuer;
};

// Result in terms of this' currency and issuer.
Amount.prototype.multiply = function (v) {
  var result;

  if (this.is_zero()) {
    result = this.clone();
  }
  else if (v.is_zero()) {
    result = this.clone();
    result._value = BigInteger.ZERO.clone();
  }
  else {
    var v1 = this._value;
    var o1 = this._offset;
    var v2 = v._value;
    var o2 = v._offset;

    while (v1.compareTo(consts.bi_man_min_value) < 0 ) {
      v1 = v1.multiply(consts.bi_10);
      o1 -= 1;
    }

    while (v2.compareTo(consts.bi_man_min_value) < 0 ) {
      v2 = v2.multiply(consts.bi_10);
      o2 -= 1;
    }

    result              = new Amount();
    result._offset      = o1 + o2 + 14;
    result._value       = v1.multiply(v2).divide(consts.bi_1e14).add(consts.bi_7);
    result._is_native   = this._is_native;
    result._is_negative = this._is_negative !== v._is_negative;
    result._currency    = this._currency.clone();
    result._issuer      = this._issuer.clone();

    result.canonicalize();
  }

  return result;
};

// Return a new value.
Amount.prototype.negate = function () {
  return this.clone('NEGATE');
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
Amount.prototype.parse_human = function (j) {
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
      this._value       = this._value.multiply(consts.bi_xns_unit).add(new BigInteger(fraction));
    }
    // Other currencies have arbitrary precision
    else {
      while (fraction[fraction.length - 1] === "0") {
        fraction = fraction.slice(0, fraction.length - 1);
      }

      precision = fraction.length;

      this._is_native   = false;
      var multiplier    = consts.bi_10.clone().pow(precision);
      this._value      	= this._value.multiply(multiplier).add(new BigInteger(fraction));
      this._offset     	= -precision;

      this.canonicalize();
    }

    this._is_negative = !!m[2];
  } else {
    this._value	      = NaN;
  }

  return this;
};

Amount.prototype.parse_issuer = function (issuer) {
  this._issuer.parse_json(issuer);

  return this;
};

// <-> j
Amount.prototype.parse_json = function (j) {
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
  else if ('number' === typeof j) {
    this.parse_json(""+j);
  }
  else if ('object' === typeof j && j instanceof Amount) {
    j.copyTo(this);
  }
  else if ('object' === typeof j && 'value' in j) {
    // Parse the passed value to sanitize and copy it.

    this._currency.parse_json(j.currency);     // Never XRP.
    if ("string" === typeof j.issuer) this._issuer.parse_json(j.issuer);
    this.parse_value(j.value);
  }
  else {
    this._value	    = NaN;
  }

  return this;
};

// Parse a XRP value from untrusted input.
// - integer = raw units
// - float = with precision 6
// XXX Improvements: disallow leading zeros.
Amount.prototype.parse_native = function (j) {
  var m;

  if ('string' === typeof j)
    m = j.match(/^(-?)(\d*)(\.\d{0,6})?$/);

  if (m) {
    if (undefined === m[3]) {
      // Integer notation

      this._value	  = new BigInteger(m[2]);
    }
    else {
      // Float notation : values multiplied by 1,000,000.

      var   int_part	  = (new BigInteger(m[2])).multiply(consts.bi_xns_unit);
      var   fraction_part = (new BigInteger(m[3])).multiply(new BigInteger(String(Math.pow(10, 1+consts.xns_precision-m[3].length))));

      this._value	  = int_part.add(fraction_part);
    }

    this._is_native   = true;
    this._offset      = 0;
    this._is_negative = !!m[1] && this._value.compareTo(BigInteger.ZERO) !== 0;

    if (this._value.compareTo(consts.bi_xns_max) > 0)
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
Amount.prototype.parse_value = function (j) {
  this._is_native    = false;

  if ('number' === typeof j) {
    this._is_negative = j < 0;
    this._value	      = new BigInteger(this._is_negative ? -j : j);
    this._offset      = 0;

    this.canonicalize();
  }
  else if ('string' === typeof j) {
    var	i = j.match(/^(-?)(\d+)$/);
    var	d = !i && j.match(/^(-?)(\d*)\.(\d*)$/);
    var	e = !e && j.match(/^(-?)(\d*)e(-?\d+)$/);

    if (e) {
      // e notation

      this._value	= new BigInteger(e[2]);
      this._offset 	= parseInt(e[3]);
      this._is_negative	= !!e[1];

      this.canonicalize();
    }
    else if (d) {
      // float notation

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
  else if (j instanceof BigInteger) {
    this._value	      = j.clone();
  }
  else {
    this._value	      = NaN;
  }

  return this;
};

Amount.prototype.set_currency = function (c) {
  if ('string' === typeof c) {
    this._currency.parse_json(c);  
  }
  else
  {
    c.copyTo(this._currency);
  }
  this._is_native = this._currency.is_native();

  return this;
};

Amount.prototype.set_issuer = function (issuer) {
  if (issuer instanceof UInt160) {
    issuer.copyTo(this._issuer);
  } else {
    this._issuer.parse_json(issuer);
  }

  return this;
};

// Result in terms of this' currency and issuer.
Amount.prototype.subtract = function (v) {
  // Correctness over speed, less code has less bugs, reuse add code.
  return this.add(v.negate());
};

Amount.prototype.to_number = function (allow_nan) {
  var s = this.to_text(allow_nan);

  return ('string' === typeof s) ? Number(s) : s;
}

// Convert only value to JSON wire format.
Amount.prototype.to_text = function (allow_nan) {
  if (!(this._value instanceof BigInteger)) {
    // Never should happen.
    return allow_nan ? NaN : "0";
  }
  else if (this._is_native) {
    if (this._value.compareTo(consts.bi_xns_max) > 0)
    {
      // Never should happen.
      return allow_nan ? NaN : "0";
    }
    else
    {
      return (this._is_negative ? "-" : "") + this._value.toString();
    }
  }
  else if (this.is_zero())
  {
    return "0";
  }
  else if (this._offset && (this._offset < -25 || this._offset > -4))
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
 * @param opts.min_precision {Number} Min. number of digits after dec. point.
 * @param opts.skip_empty_fraction {Boolean} Don't show fraction if it is zero,
 *   even if min_precision is set.
 * @param opts.group_sep {Boolean|String} Whether to show a separator every n
 *   digits, if a string, that value will be used as the separator. Default: ","
 * @param opts.group_width {Number} How many numbers will be grouped together,
 *   default: 3.
 * @param opts.signed {Boolean|String} Whether negative numbers will have a
 *   prefix. If String, that string will be used as the prefix. Default: "-"
 */
Amount.prototype.to_human = function (opts)
{
  opts = opts || {};

  if (!this.is_valid()) return "";

  // Default options
  if ("undefined" === typeof opts.signed) opts.signed = true;
  if ("undefined" === typeof opts.group_sep) opts.group_sep = true;
  opts.group_width = opts.group_width || 3;

  var order = this._is_native ? consts.xns_precision : -this._offset;
  var denominator = consts.bi_10.clone().pow(order);
  var int_part = this._value.divide(denominator).toString(10);
  var fraction_part = this._value.mod(denominator).toString(10);

  // Add leading zeros to fraction
  while (fraction_part.length < order) {
    fraction_part = "0" + fraction_part;
  }

  int_part = int_part.replace(/^0*/, '');
  fraction_part = fraction_part.replace(/0*$/, '');

  if (fraction_part.length || !opts.skip_empty_fraction) {
    if ("number" === typeof opts.precision) {
      fraction_part = fraction_part.slice(0, opts.precision);
    }

    if ("number" === typeof opts.min_precision) {
      while (fraction_part.length < opts.min_precision) {
        fraction_part += "0";
      }
    }
  }

  if (opts.group_sep) {
    if ("string" !== typeof opts.group_sep) {
      opts.group_sep = ',';
    }
    int_part = utils.chunkString(int_part, opts.group_width, true).join(opts.group_sep);
  }

  var formatted = '';
  if (opts.signed && this._is_negative) {
    if ("string" !== typeof opts.signed) {
      opts.signed = '-';
    }
    formatted += opts.signed;
  }
  formatted += int_part.length ? int_part : '0';
  formatted += fraction_part.length ? '.'+fraction_part : '';

  return formatted;
};

Amount.prototype.to_human_full = function (opts) {
  opts = opts || {};

  var a = this.to_human(opts);
  var c = this._currency.to_human();
  var i = this._issuer.to_json(opts);

  var o;

  if (this._is_native)
  {
    o = a + "/" + c;
  }
  else
  {
    o = a + "/" + c + "/" + i;
  }

  return o;
};

Amount.prototype.to_json = function () {
  if (this._is_native) {
    return this.to_text();
  }
  else
  {
    var amount_json = {
      'value' : this.to_text(),
      'currency' : this._currency.to_json()
    };
    if (this._issuer.is_valid()) {
      amount_json.issuer = this._issuer.to_json();
    }
    return amount_json;
  }
};

Amount.prototype.to_text_full = function (opts) {
  return this._value instanceof BigInteger
    ? this._is_native
      ? this.to_text() + "/XRP"
      : this.to_text() + "/" + this._currency.to_json() + "/" + this._issuer.to_json(opts)
    : NaN;
};

// For debugging.
Amount.prototype.not_equals_why = function (d) {
  return 'string' === typeof (d)
    ? this.not_equals_why(Amount.from_json(d))
    : this === d
      ? false
      : d instanceof Amount
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

// DEPRECATED: Include the corresponding files instead.
exports.Currency      = Currency;
exports.Seed          = Seed;
exports.UInt160	      = UInt160;

// vim:sw=2:sts=2:ts=8:et
