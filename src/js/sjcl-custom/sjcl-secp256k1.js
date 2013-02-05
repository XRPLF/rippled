// ----- for secp256k1 ------

// Overwrite NIST-P256 with secp256k1 so we're on even footing
sjcl.ecc.curves.c256 = new sjcl.ecc.curve(
    sjcl.bn.pseudoMersennePrime(256, [[0,-1],[4,-1],[6,-1],[7,-1],[8,-1],[9,-1],[32,-1]]),
    "0x14551231950b75fc4402da1722fc9baee",
    0,
    7,
    "0x79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",
    "0x483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8"
);

// Replace point addition and doubling algorithms
// NIST-P256 is a=-3, we need algorithms for a=0
sjcl.ecc.pointJac.prototype.add = function(T) {
  var S = this;
  if (S.curve !== T.curve) {
    throw("sjcl.ecc.add(): Points must be on the same curve to add them!");
  }

  if (S.isIdentity) {
    return T.toJac();
  } else if (T.isIdentity) {
    return S;
  }

  var z1z1 = S.z.square();
  var h = T.x.mul(z1z1).subM(S.x);
  var s2 = T.y.mul(S.z).mul(z1z1);

  if (h.equals(0)) {
    if (S.y.equals(T.y.mul(z1z1.mul(S.z)))) {
      // same point
      return S.doubl();
    } else {
      // inverses
      return new sjcl.ecc.pointJac(S.curve);
    }
  }

  var hh = h.square();
  var i = hh.copy().doubleM().doubleM();
  var j = h.mul(i);
  var r = s2.sub(S.y).doubleM();
  var v = S.x.mul(i);
  
  var x = r.square().subM(j).subM(v.copy().doubleM());
  var y = r.mul(v.sub(x)).subM(S.y.mul(j).doubleM());
  var z = S.z.add(h).square().subM(z1z1).subM(hh);

  return new sjcl.ecc.pointJac(this.curve,x,y,z);
};

sjcl.ecc.pointJac.prototype.doubl = function () {
  if (this.isIdentity) { return this; }

  var a = this.x.square();
  var b = this.y.square();
  var c = b.square();
  var d = this.x.add(b).square().subM(a).subM(c).doubleM();
  var e = a.mul(3);
  var f = e.square();
  var x = f.sub(d.copy().doubleM());
  var y = e.mul(d.sub(x)).subM(c.doubleM().doubleM().doubleM());
  var z = this.y.mul(this.z).doubleM();
  return new sjcl.ecc.pointJac(this.curve, x, y, z);
};

sjcl.ecc.point.prototype.toBytesCompressed = function () {
  var header = this.y.mod(2).toString() == "0x0" ? 0x02 : 0x03;
  return [header].concat(sjcl.codec.bytes.fromBits(this.x.toBits()))
};
