var sjcl    = require('../../build/sjcl');

var UInt256 = require('./uint256').UInt256;

var KeyPair = function ()
{
  this._curve = sjcl.ecc.curves['c256'];
  this._secret = null;
  this._pubkey = null;
};

KeyPair.from_bn_secret = function (j)
{
  if (j instanceof this) {
    return j.clone();
  } else {
    return (new this()).parse_bn_secret(j);
  }
};

KeyPair.prototype.parse_bn_secret = function (j)
{
  this._secret = new sjcl.ecc.ecdsa.secretKey(sjcl.ecc.curves['c256'], j);
  return this;
};

/**
 * Returns public key as sjcl public key.
 *
 * @private
 */
KeyPair.prototype._pub = function ()
{
  var curve = this._curve;

  if (!this._pubkey && this._secret) {
    var exponent = this._secret._exponent;
    this._pubkey = new sjcl.ecc.ecdsa.publicKey(curve, curve.G.mult(exponent));
  }

  return this._pubkey;
};

/**
 * Returns public key as hex.
 *
 * Key will be returned as a compressed pubkey - 33 bytes converted to hex.
 */
KeyPair.prototype.to_hex_pub = function ()
{
  var pub = this._pub();
  if (!pub) return null;

  var point = pub._point, y_even = point.y.mod(2).equals(0);
  return sjcl.codec.hex.fromBits(sjcl.bitArray.concat(
    [sjcl.bitArray.partial(8, y_even ? 0x02 : 0x03)],
    point.x.toBits(this._curve.r.bitLength())
  )).toUpperCase();
};

KeyPair.prototype.sign = function (hash)
{
  hash = UInt256.from_json(hash);
  return this._secret.signDER(hash.to_bits(), 0);
};

exports.KeyPair = KeyPair;
