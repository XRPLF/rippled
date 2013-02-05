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
    Amount  = amount.Amount;

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

exports.Int8 = new SerializedType({
  serialize: function (so, val) {
    so.append([val & 0xff]);
  },
  parse: function (so) {
    return so.read(1)[0];
  }
});

exports.Int16 = new SerializedType({
  serialize: function (so, val) {
    so.append([
      val >>> 8 & 0xff,
      val       & 0xff
    ]);
  },
  parse: function (so) {
    // XXX
  }
});

exports.Int32 = new SerializedType({
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
  }
});

exports.Int64 = new SerializedType({
  serialize: function (so, val) {
    // XXX
  },
  parse: function (so) {
    // XXX
  }
});

exports.Hash128 = new SerializedType({
  serialize: function (so, val) {
    // XXX
  },
  parse: function (so) {
    // XXX
  }
});

exports.Hash256 = new SerializedType({
  serialize: function (so, val) {
    // XXX
  },
  parse: function (so) {
    // XXX
  }
});

exports.Hash160 = new SerializedType({
  serialize: function (so, val) {
    // XXX
  },
  parse: function (so) {
    // XXX
  }
});

exports.Amount = new SerializedType({
  serialize: function (so, val) {
    var amount = Amount.from_json(val);
    if (!amount.is_valid()) {
      throw new Error("Not a valid Amount object.");
    }

    // Amount (64-bit integer)
    if (amount.is_native()) {
      var valueHex = amount._value.toString(16);

      // Enforce correct length (64 bits)
      if (valueHex.length > 16) {
        throw new Error('Value out of bounds');
      }
      while (valueHex.length < 16) {
        valueHex = "0" + valueHex;
      }

      var valueBytes = bytes.fromBits(hex.toBits(valueHex));
      // Clear most significant two bits - these bits should already be 0 if
      // Amount enforces the range correctly, but we'll clear them anyway just
      // so this code can make certain guarantees about the encoded value.
      valueBytes[0] &= 0x3f;
      if (!amount.is_negative()) valueBytes[0] |= 0x40;

      so.append(valueBytes);
    } else {
      // XXX
      throw new Error("Non-native amounts not implemented!");
    }

    if (!amount.is_native()) {
      // Currency (160-bit hash)
      var currency = amount.currency().to_json();
      if ("string" === typeof currency && currency.length === 3) {
        var currencyCode = currency.toUpperCase(),
            currencyData = utils.arraySet(20, 0);

        if (!/^[A-Z]{3}$/.test(currencyCode)) {
          throw new Error('Invalid currency code');
        }

        currencyData[12] = currencyCode.charCodeAt(0) & 0xff;
        currencyData[13] = currencyCode.charCodeAt(1) & 0xff;
        currencyData[14] = currencyCode.charCodeAt(2) & 0xff;

        var currencyBits = bytes.toBits(currencyData),
            currencyHash = sjcl.hash.ripemd160.hash(currencyBits);

        so.append(bytes.fromBits(currencyHash));
      } else {
        throw new Error('Tried to serialize invalid/unimplemented currency type.');
      }

      // Issuer (160-bit hash)
      // XXX
    }
  },
  parse: function (so) {
    // XXX
  }
});

exports.VariableLength = new SerializedType({
  serialize: function (so, val) {
    if ("string" === typeof val) this.serialize_hex(so, val);
    else throw new Error("Unknown datatype.");
  },
  parse: function (so) {
    // XXX
  }
});

exports.Account = new SerializedType({
  serialize: function (so, val) {
    var account = UInt160.from_json(val);
    this.serialize_hex(so, account.to_hex());
  },
  parse: function (so) {
    // XXX
  }
});

exports.PathSet = new SerializedType({
  serialize: function (so, val) {
    // XXX
  },
  parse: function (so) {
    // XXX
  }
});

exports.Vector256 = new SerializedType({
  serialize: function (so, val) {
    // XXX
  },
  parse: function (so) {
    // XXX
  }
});

exports.Object = new SerializedType({
  serialize: function (so, val) {
    // XXX
  },
  parse: function (so) {
    // XXX
  }
});

exports.Array = new SerializedType({
  serialize: function (so, val) {
    // XXX
  },
  parse: function (so) {
    // XXX
  }
});
