
//
// Currency support
//

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
Currency.json_rewrite = function (j) {
  return Currency.from_json(j).to_json();
};

Currency.from_json = function (j) {
  return 'string' === typeof j
      ? (new Currency()).parse_json(j)
      : j.clone();
};

Currency.is_valid = function (j) {
  return Currency.from_json(j).is_valid();
};

Currency.prototype.clone = function() {
  return this.copyTo(new Currency());
};

// Returns copy.
Currency.prototype.copyTo = function (d) {
  d._value = this._value;

  return d;
};

Currency.prototype.equals = function (d) {
  return ('string' !== typeof this._value && isNaN(this._value))
    || ('string' !== typeof d._value && isNaN(d._value)) ? false : this._value === d._value;
};

// this._value = NaN on error.
Currency.prototype.parse_json = function (j) {
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

Currency.prototype.is_native = function () {
  return !isNaN(this._value) && !this._value;
};

Currency.prototype.is_valid = function () {
  return !isNaN(this._value);
};

Currency.prototype.to_json = function () {
  return this._value ? this._value : "XRP";
};

Currency.prototype.to_human = function () {
  return this._value ? this._value : "XRP";
};

exports.Currency = Currency;

// vim:sw=2:sts=2:ts=8:et
