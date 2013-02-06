//
// Seed support
//

var sjcl    = require('../../build/sjcl');
var utils   = require('./utils');
var jsbn    = require('./jsbn');
var extend  = require('extend');

var BigInteger = jsbn.BigInteger;

var Base = require('./base').Base,
    UInt = require('./uint').UInt,
    UInt256 = require('./uint256').UInt256,
    KeyPair = require('./keypair').KeyPair;

var Seed = extend(function () {
  // Internal form: NaN or BigInteger
  this._curve = sjcl.ecc.curves['c256'];
  this._value = NaN;
}, UInt);

Seed.width = 16;
Seed.prototype = extend({}, UInt.prototype);
Seed.prototype.constructor = Seed;

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

Seed.prototype.to_json = function () {
  if (!(this._value instanceof BigInteger))
    return NaN;

  var output = Base.encode_check(Base.VER_FAMILY_SEED, this.to_bytes());

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

Seed.prototype.get_key = function (account_id) {
  // XXX Should loop over keys until we find the right one

  var curve = this._curve;

  var seq = 0;

  var private_gen, public_gen, i = 0;
  do {
    private_gen = sjcl.bn.fromBits(firstHalfOfSHA512(append_int(this.to_bytes(), i)));
    i++;
  } while (!curve.r.greaterEquals(private_gen));

  public_gen = curve.G.mult(private_gen);

  var sec;
  i = 0;
  do {
    sec = sjcl.bn.fromBits(firstHalfOfSHA512(append_int(append_int(public_gen.toBytesCompressed(), seq), i)));
    i++;
  } while (!curve.r.greaterEquals(sec));

  sec = sec.add(private_gen).mod(curve.r);

  return KeyPair.from_bn_secret(sec);
};

exports.Seed = Seed;
