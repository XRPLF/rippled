/**
 * Type definitions for binary format.
 *
 * This file should not be included directly. Instead, find the format you're
 * trying to parse or serialize in binformat.js and pass that to
 * SerializedObject.parse() or SerializedObject.serialize().
 */

var extend  = require('extend'),
    utils   = require('./utils'),
    sjcl    = require('../../build/sjcl');

var amount  = require('./amount'),
    UInt160 = amount.UInt160,
    Amount  = amount.Amount,
    Currency= amount.Currency;

// Shortcuts
var hex    = sjcl.codec.hex,
    bytes  = sjcl.codec.bytes;

var SerializedType = function (methods) {
  extend(this, methods);
};

SerializedType.prototype.serialize_hex = function (so, hexData) {
  var byteData = bytes.fromBits(hex.toBits(hexData));
  this.serialize_varint(so, byteData.length);
  so.append(byteData);
};

SerializedType.prototype.serialize_varint = function (so, val) {
  if (val < 0) {
    throw new Error("Variable integers are unsigned.");
  }
  if (val <= 192) {
    so.append([val]);
  } else if (val <= 12,480) {
    val -= 193;
    so.append([193 + (val >>> 8), val & 0xff]);
  } else if (val <= 918744) {
    val -= 12481;
    so.append([
      241 + (val >>> 16),
      val >>> 8 & 0xff,
      val & 0xff
    ]);
  } else throw new Error("Variable integer overflow.");
};

var STInt8 = exports.Int8 = new SerializedType({
  serialize: function (so, val) {
    so.append([val & 0xff]);
  },
  parse: function (so) {
    return so.read(1)[0];
  }
});

var STInt16 = exports.Int16 = new SerializedType({
  serialize: function (so, val) {
    so.append([
      val >>> 8 & 0xff,
      val       & 0xff
    ]);
  },
  parse: function (so) {
    // XXX
    throw new Error("Parsing Int16 not implemented");
  }
});

var STInt32 = exports.Int32 = new SerializedType({
  serialize: function (so, val) {
    so.append([
      val >>> 24 & 0xff,
      val >>> 16 & 0xff,
      val >>>  8 & 0xff,
      val        & 0xff
    ]);
  },
  parse: function (so) {
    // XXX
    throw new Error("Parsing Int32 not implemented");
  }
});

var STInt64 = exports.Int64 = new SerializedType({
  serialize: function (so, val) {
    // XXX
    throw new Error("Serializing Int64 not implemented");
  },
  parse: function (so) {
    // XXX
    throw new Error("Parsing Int64 not implemented");
  }
});

var STHash128 = exports.Hash128 = new SerializedType({
  serialize: function (so, val) {
    // XXX
    throw new Error("Serializing Hash128 not implemented");
  },
  parse: function (so) {
    // XXX
    throw new Error("Parsing Hash128 not implemented");
  }
});

var STHash256 = exports.Hash256 = new SerializedType({
  serialize: function (so, val) {
    // XXX
    throw new Error("Serializing Hash256 not implemented");
  },
  parse: function (so) {
    // XXX
    throw new Error("Parsing Hash256 not implemented");
  }
});

var STHash160 = exports.Hash160 = new SerializedType({
  serialize: function (so, val) {
    // XXX
    throw new Error("Serializing Hash160 not implemented");
  },
  parse: function (so) {
    // XXX
    throw new Error("Parsing Hash160 not implemented");
  }
});

// Internal
var STCurrency = new SerializedType({
  serialize: function (so, val) {
    var currency = val.to_json();
    if ("string" === typeof currency && currency.length === 3) {
      var currencyCode = currency.toUpperCase(),
          currencyData = utils.arraySet(20, 0);

      if (!/^[A-Z]{3}$/.test(currencyCode)) {
        throw new Error('Invalid currency code');
      }

      currencyData[12] = currencyCode.charCodeAt(0) & 0xff;
      currencyData[13] = currencyCode.charCodeAt(1) & 0xff;
      currencyData[14] = currencyCode.charCodeAt(2) & 0xff;

      so.append(currencyData);
    } else {
      throw new Error('Tried to serialize invalid/unimplemented currency type.');
    }
  },
  parse: function (so) {
    // XXX
    throw new Error("Parsing Currency not implemented");
  }
});

var STAmount = exports.Amount = new SerializedType({
  serialize: function (so, val) {
    var amount = Amount.from_json(val);
    if (!amount.is_valid()) {
      throw new Error("Not a valid Amount object.");
    }

    // Amount (64-bit integer)
    var valueBytes = utils.arraySet(8, 0);
    if (amount.is_native()) {
      var valueHex = amount._value.toString(16);

      // Enforce correct length (64 bits)
      if (valueHex.length > 16) {
        throw new Error('Value out of bounds');
      }
      while (valueHex.length < 16) {
        valueHex = "0" + valueHex;
      }

      valueBytes = bytes.fromBits(hex.toBits(valueHex));
      // Clear most significant two bits - these bits should already be 0 if
      // Amount enforces the range correctly, but we'll clear them anyway just
      // so this code can make certain guarantees about the encoded value.
      valueBytes[0] &= 0x3f;
      if (!amount.is_negative()) valueBytes[0] |= 0x40;
    } else {
      var hi = 0, lo = 0;

      // First bit: non-native
      hi |= 1 << 31;

      if (!amount.is_zero()) {
        // Second bit: non-negative?
        if (!amount.is_negative()) hi |= 1 << 30;

        // Next eight bits: offset/exponent
        hi |= ((97 + amount._offset) & 0xff) << 22;

        // Remaining 52 bits: mantissa
        hi |= amount._value.shiftRight(32).intValue() & 0x3fffff;
        lo = amount._value.intValue() & 0xffffffff;
      }

      valueBytes = sjcl.codec.bytes.fromBits([hi, lo]);
    }

    so.append(valueBytes);

    if (!amount.is_native()) {
      // Currency (160-bit hash)
      var currency = amount.currency();
      STCurrency.serialize(so, currency);

      // Issuer (160-bit hash)
      so.append(amount.issuer().to_bytes());
    }
  },
  parse: function (so) {
    // XXX
    throw new Error("Parsing Amount not implemented");
  }
});

var STVL = exports.VariableLength = new SerializedType({
  serialize: function (so, val) {
    if ("string" === typeof val) this.serialize_hex(so, val);
    else throw new Error("Unknown datatype.");
  },
  parse: function (so) {
    // XXX
    throw new Error("Parsing VL not implemented");
  }
});

var STAccount = exports.Account = new SerializedType({
  serialize: function (so, val) {
    var account = UInt160.from_json(val);
    this.serialize_hex(so, account.to_hex());
  },
  parse: function (so) {
    // XXX
    throw new Error("Parsing Account not implemented");
  }
});

var STPathSet = exports.PathSet = new SerializedType({
  serialize: function (so, val) {
    // XXX
    for (var i = 0, l = val.length; i < l; i++) {
      for (var j = 0, l2 = val[i].length; j < l2; j++) {
        var entry = val[i][j];

        var type = 0;

        if (entry.account) type |= 0x01;
        if (entry.currency) type |= 0x10;
        if (entry.issuer) type |= 0x20;

        STInt8.serialize(so, type);

        if (entry.account) {
          so.append(UInt160.from_json(entry.account).to_bytes());
        }
        if (entry.currency) {
          var currency = Currency.from_json(entry.currency);
          STCurrency.serialize(so, currency);
        }
        if (entry.issuer) {
          so.append(UInt160.from_json(entry.issuer).to_bytes());
        }
      }

      if (j < l2) STInt8.serialize(so, 0xff);
    }
    STInt8.serialize(so, 0x00);
  },
  parse: function (so) {
    // XXX
    throw new Error("Parsing PathSet not implemented");
  }
});

var STVector256 = exports.Vector256 = new SerializedType({
  serialize: function (so, val) {
    // XXX
    throw new Error("Serializing Vector256 not implemented");
  },
  parse: function (so) {
    // XXX
    throw new Error("Parsing Vector256 not implemented");
  }
});

var STObject = exports.Object = new SerializedType({
  serialize: function (so, val) {
    // XXX
    throw new Error("Serializing Object not implemented");
  },
  parse: function (so) {
    // XXX
    throw new Error("Parsing Object not implemented");
  }
});

var STArray = exports.Array = new SerializedType({
  serialize: function (so, val) {
    // XXX
    throw new Error("Serializing Array not implemented");
  },
  parse: function (so) {
    // XXX
    throw new Error("Parsing Array not implemented");
  }
});
