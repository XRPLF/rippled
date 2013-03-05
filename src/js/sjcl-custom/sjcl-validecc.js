sjcl.ecc.ecdsa.secretKey.prototype = {
  sign: function(hash, paranoia) {
    var R = this._curve.r,
        l = R.bitLength(),
        k = sjcl.bn.random(R.sub(1), paranoia).add(1),
        r = this._curve.G.mult(k).x.mod(R),
        s = sjcl.bn.fromBits(hash).add(r.mul(this._exponent)).mul(k.inverseMod(R)).mod(R);

    return sjcl.bitArray.concat(r.toBits(l), s.toBits(l));
  }
};

sjcl.ecc.ecdsa.publicKey.prototype = {
  verify: function(hash, rs) {
    var w = sjcl.bitArray,
        R = this._curve.r,
        l = R.bitLength(),
        r = sjcl.bn.fromBits(w.bitSlice(rs,0,l)),
        s = sjcl.bn.fromBits(w.bitSlice(rs,l,2*l)),
        sInv = s.modInverse(R),
        hG = sjcl.bn.fromBits(hash).mul(sInv).mod(R),
        hA = r.mul(sInv).mod(R),
        r2 = this._curve.G.mult2(hG, hA, this._point).x;

    if (r.equals(0) || s.equals(0) || r.greaterEquals(R) || s.greaterEquals(R) || !r2.equals(r)) {
      throw (new sjcl.exception.corrupt("signature didn't check out"));
    }
    return true;
  }
};
