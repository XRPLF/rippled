sjcl.bn.ZERO = new sjcl.bn(0);

/** [ this / that , this % that ] */
sjcl.bn.prototype.divRem = function (that) {
  if (typeof(that) !== "object") { that = new this._class(that); }
  var thisa = this.abs(), thata = that.abs(), quot = new this._class(0),
      ci = 0;
  if (!thisa.greaterEquals(thata)) {
    this.initWith(0);
    return this;
  } else if (thisa.equals(thata)) {
    this.initWith(sign);
    return this;
  }

  for (; thisa.greaterEquals(thata); ci++) {
    thata.doubleM();
  }
  for (; ci > 0; ci--) {
    quot.doubleM();
    thata.halveM();
    if (thisa.greaterEquals(thata)) {
      quot.addM(1);
      thisa.subM(that).normalize();
    }
  }
  return [quot, thisa];
};

/** this /= that (rounded to nearest int) */
sjcl.bn.prototype.divRound = function (that) {
  var dr = this.divRem(that), quot = dr[0], rem = dr[1];

  if (rem.doubleM().greaterEquals(that)) {
    quot.addM(1);
  }

  return quot;
};

/** this /= that (rounded down) */
sjcl.bn.prototype.div = function (that) {
  var dr = this.divRem(that);
  return dr[0];
};

sjcl.bn.prototype.sign = function () {
      return this.greaterEquals(sjcl.bn.ZERO) ? 1 : -1;
    };

/** -this */
sjcl.bn.prototype.neg = function () {
  return sjcl.bn.ZERO.sub(this);
};

/** |this| */
sjcl.bn.prototype.abs = function () {
  if (this.sign() === -1) {
    return this.neg();
  } else return this;
};
