var SerializedType = function () {

};

exports.Int8 = new SerializedType({
  serialize: function (so, val) {
    return so.append([val & 0xff]);
  },
  parse: function (so) {
    return so.read(1)[0];
  }
});

exports.Int16 = new SerializedType({
  serialize: function (so, val) {
    // XXX
  },
  parse: function (so) {
    // XXX
  }
});

exports.Int32 = new SerializedType({
  serialize: function (so, val) {
    // XXX
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
    // XXX
  },
  parse: function (so) {
    // XXX
  }
});

exports.VariableLength = new SerializedType({
  serialize: function (so, val) {
    // XXX
  },
  parse: function (so) {
    // XXX
  }
});

exports.Account = new SerializedType({
  serialize: function (so, val) {
    // XXX
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
