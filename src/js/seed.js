//
// Seed support
//

var utils   = require('./utils');
var jsbn    = require('./jsbn');

var BigInteger = jsbn.BigInteger;

var Base = require('./base').Base;

var Seed = function () {
  // Internal form: NaN or BigInteger
  this._value  = NaN;
};

Seed.json_rewrite = function (j) {
  return Seed.from_json(j).to_json();
};

// Return a new Seed from j.
Seed.from_json = function (j) {
  return 'string' === typeof j
      ? (new Seed()).parse_json(j)
      : j.clone();
};

Seed.is_valid = function (j) {
  return Seed.from_json(j).is_valid();
};

Seed.prototype.clone = function () {
  return this.copyTo(new Seed());
};

// Returns copy.
Seed.prototype.copyTo = function (d) {
  d._value = this._value;

  return d;
};

Seed.prototype.equals = function (d) {
  return this._value instanceof BigInteger && d._value instanceof BigInteger && this._value.equals(d._value);
};

Seed.prototype.is_valid = function () {
  return this._value instanceof BigInteger;
};

// value = NaN on error.
// One day this will support rfc1751 too.
Seed.prototype.parse_json = function (j) {
  if ('string' !== typeof j) {
    this._value  = NaN;
  }
  else if (j[0] === "s") {
    this._value  = Base.decode_check(Base.VER_FAMILY_SEED, j);
  }
  else if (16 === j.length) {
    this._value  = new BigInteger(utils.stringToArray(j), 128);
  }
  else {
    this._value  = NaN;
  }

  return this;
};

// Convert from internal form.
Seed.prototype.to_json = function () {
  if (!(this._value instanceof BigInteger))
    return NaN;

  var bytes   = this._value.toByteArray().map(function (b) { return b ? b < 0 ? 256+b : b : 0; });
  var target  = 20;

  // XXX Make sure only trim off leading zeros.
  var array = bytes.length < target
		? bytes.length
		  ? [].concat(utils.arraySet(target - bytes.length, 0), bytes)
		  : utils.arraySet(target, 0)
		: bytes.slice(target - bytes.length);
  var output = Base.encode_check(Base.VER_FAMILY_SEED, array);

  return output;
};

exports.Seed = Seed;
