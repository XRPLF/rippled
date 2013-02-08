sjcl.ecc.ecdsa.secretKey.prototype.signDER = function(hash, paranoia) {
  return this.encodeDER(this.sign(hash, paranoia));
};

sjcl.ecc.ecdsa.secretKey.prototype.encodeDER = function(rs) {
  var w = sjcl.bitArray,
      R = this._curve.r,
      l = R.bitLength();

  var rb = sjcl.codec.bytes.fromBits(w.bitSlice(rs,0,l)),
      sb = sjcl.codec.bytes.fromBits(w.bitSlice(rs,l,2*l));

  // Drop empty leading bytes
  while (!rb[0] && rb.length) rb.shift();
  while (!sb[0] && sb.length) sb.shift();

  // If high bit is set, prepend an extra zero byte (DER signed integer)
  if (rb[0] & 0x80) rb.unshift(0);
  if (sb[0] & 0x80) sb.unshift(0);

  var buffer = [].concat(
    0x30,
    4 + rb.length + sb.length,
    0x02,
    rb.length,
    rb,
    0x02,
    sb.length,
    sb
  );

  return sjcl.codec.bytes.toBits(buffer);
};

