var binformat = require('./binformat'),
    sjcl = require('../../build/sjcl'),
    extend = require('extend'),
    stypes = require('./serializedtypes');

var UInt256 = require('./uint256').UInt256;

var SerializedObject = function () {
  this.buffer = [];
  this.pointer = 0;
};

SerializedObject.from_json = function (obj) {
  var typedef;
  var so = new SerializedObject();

  // Create a copy of the object so we don't modify it
  obj = extend({}, obj);

  if ("number" === typeof obj.TransactionType) {
    obj.TransactionType = SerializedObject.lookup_type_tx(obj.TransactionType);

    if (!obj.TransactionType) {
      throw new Error("Transaction type ID is invalid.");
    }
  }

  if ("string" === typeof obj.TransactionType) {
    typedef = binformat.tx[obj.TransactionType].slice();

    obj.TransactionType = typedef.shift();
  } else if ("undefined" !== typeof obj.LedgerEntryType) {
    // XXX: TODO
    throw new Error("Ledger entry binary format not yet implemented.");
  } else throw new Error("Object to be serialized must contain either " +
                         "TransactionType or LedgerEntryType.");

  so.serialize(typedef, obj);

  return so;
};

SerializedObject.prototype.append = function (bytes) {
  this.buffer = this.buffer.concat(bytes);
  this.pointer += bytes.length;
};

SerializedObject.prototype.to_bits = function ()
{
  return sjcl.codec.bytes.toBits(this.buffer);
};

SerializedObject.prototype.to_hex = function () {
  return sjcl.codec.hex.fromBits(this.to_bits()).toUpperCase();
};

SerializedObject.prototype.serialize = function (typedef, obj)
{
  // Ensure canonical order
  typedef = SerializedObject._sort_typedef(typedef.slice());

  // Serialize fields
  for (var i = 0, l = typedef.length; i < l; i++) {
    var spec = typedef[i];
    this.serialize_field(spec, obj);
  }
};

SerializedObject.prototype.signing_hash = function (prefix)
{
  var sign_buffer = new SerializedObject();
  stypes.Int32.serialize(sign_buffer, prefix);
  sign_buffer.append(this.buffer);
  return sign_buffer.hash_sha512_half();
};

SerializedObject.prototype.hash_sha512_half = function ()
{
  var bits = sjcl.codec.bytes.toBits(this.buffer),
      hash = sjcl.bitArray.bitSlice(sjcl.hash.sha512.hash(bits), 0, 256);

  return UInt256.from_hex(sjcl.codec.hex.fromBits(hash));
};

SerializedObject.prototype.serialize_field = function (spec, obj)
{
  spec = spec.slice();

  var name = spec.shift(),
      presence = spec.shift(),
      field_id = spec.shift(),
      Type = spec.shift();

  if ("undefined" !== typeof obj[name]) {
    console.log(name, Type.id, field_id);
    this.append(SerializedObject.get_field_header(Type.id, field_id));

    try {
      Type.serialize(this, obj[name]);
    } catch (e) {
      // Add field name to message and rethrow
      e.message = "Error serializing '"+name+"': "+e.message;
      throw e;
    }
  } else if (presence === binformat.REQUIRED) {
    throw new Error('Missing required field '+name);
  }
};

SerializedObject.get_field_header = function (type_id, field_id)
{
  var buffer = [0];
  if (type_id > 0xf) buffer.push(type_id & 0xff);
  else buffer[0] += (type_id & 0xf) << 4;

  if (field_id > 0xf) buffer.push(field_id & 0xff);
  else buffer[0] += field_id & 0xf;

  return buffer;
};

function sort_field_compare(a, b) {
  // Sort by type id first, then by field id
  return a[3].id !== b[3].id ?
    a[3].id - b[3].id :
    a[2] - b[2];
};
SerializedObject._sort_typedef = function (typedef) {
  return typedef.sort(sort_field_compare);
};

SerializedObject.lookup_type_tx = function (id) {
  for (var i in binformat.tx) {
    if (!binformat.tx.hasOwnProperty(i)) continue;

    if (binformat.tx[i][0] === id) {
      return i;
    }
  }

  return null;
};

exports.SerializedObject = SerializedObject;
