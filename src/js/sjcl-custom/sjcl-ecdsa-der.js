sjcl.ecc.ecdsa.secretKey.prototype.signDER = function(hash, paranoia) {
  return this.encodeDER(this.sign(hash, paranoia));
};

sjcl.ecc.ecdsa.secretKey.prototype.encodeDER = function(rs) {
  var w = sjcl.bitArray,
      R = this._curve.r,
      l = R.bitLength(),
      r = sjcl.bn.fromBits(w.bitSlice(rs,0,l)).toBits(),
      s = sjcl.bn.fromBits(w.bitSlice(rs,l,2*l)).toBits();

  var rb = sjcl.codec.bytes.fromBits(r),
      sb = sjcl.codec.bytes.fromBits(s);

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

