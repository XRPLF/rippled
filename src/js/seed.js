//
// Seed support
//

var sjcl    = require('../../build/sjcl');
var utils   = require('./utils');
var jsbn    = require('./jsbn');

var BigInteger = jsbn.BigInteger;

var Base = require('./base').Base,
    UInt256 = require('./uint256').UInt256;

var Seed = function () {
  // Internal form: NaN or BigInteger
  this._value  = NaN;
};

Seed.json_rewrite = function (j) {
  return Seed.from_json(j).to_json();
};

// Return a new Seed from j.
Seed.from_json = function (j) {
  return (j instanceof Seed)
    ? j.clone()
    : (new Seed()).parse_json(j);
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

function append_int(a, i) {
  return [].concat(a, i >> 24, (i >> 16) & 0xff, (i >> 8) & 0xff, i & 0xff);
}

function firstHalfOfSHA512(bytes) {
  return sjcl.bitArray.bitSlice(
    sjcl.hash.sha512.hash(sjcl.codec.bytes.toBits(bytes)),
    0, 256
  );
}

function SHA256_RIPEMD160(bits) {
  return sjcl.hash.ripemd160.hash(sjcl.hash.sha256.hash(bits));
}

Seed.prototype.generate_private = function (account_id) {
  // XXX If account_id is given, should loop over keys until we find the right one

  var seq = 0;

  var private_gen, public_gen, i = 0;
  do {
    private_gen = sjcl.bn.fromBits(firstHalfOfSHA512(append_int(this.seed, i)));
    i++;
  } while (!sjcl.ecc.curves.c256.r.greaterEquals(private_gen));

  public_gen = sjcl.ecc.curves.c256.G.mult(private_gen);

  var sec;
  i = 0;
  do {
    sec = sjcl.bn.fromBits(firstHalfOfSHA512(append_int(append_int(public_gen.toBytesCompressed(), seq), i)));
    i++;
  } while (!sjcl.ecc.curves.c256.r.greaterEquals(sec));

  return UInt256.from_bn(sec);
};

exports.Seed = Seed;
