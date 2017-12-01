'use strict';

var _Object$keys = require('babel-runtime/core-js/object/keys')['default'];

var assert = require('assert');
var extend = require('extend');
var BN = require('bn.js');
var hashjs = require('hash.js');
var sjclcodec = require('sjcl-codec');
var binformat = require('./binformat');
var stypes = require('./serializedtypes');
var utils = require('./utils');
var UInt256 = require('./uint256').UInt256;

var TRANSACTION_TYPES = {};

_Object$keys(binformat.tx).forEach(function (key) {
  TRANSACTION_TYPES[binformat.tx[key][0]] = key;
});

var LEDGER_ENTRY_TYPES = {};

_Object$keys(binformat.ledger).forEach(function (key) {
  LEDGER_ENTRY_TYPES[binformat.ledger[key][0]] = key;
});

var TRANSACTION_RESULTS = {};

_Object$keys(binformat.ter).forEach(function (key) {
  TRANSACTION_RESULTS[binformat.ter[key]] = key;
});

function fieldType(fieldName) {
  var fieldDef = binformat.fieldsInverseMap[fieldName];
  return binformat.types[fieldDef[0]];
}

function SerializedObject(buf) {
  if (Array.isArray(buf) || Buffer && Buffer.isBuffer(buf)) {
    this.buffer = buf;
  } else if (typeof buf === 'string') {
    this.buffer = sjclcodec.bytes.fromBits(sjclcodec.hex.toBits(buf));
  } else if (!buf) {
    this.buffer = [];
  } else {
    throw new Error('Invalid buffer passed.');
  }
  this.pointer = 0;
}

SerializedObject.from_json = function (obj) {
  var so = new SerializedObject();
  so.parse_json(obj);
  return so;
};

SerializedObject.check_fields = function (typedef, obj) {
  var missingFields = [];
  var unknownFields = [];
  var fieldsMap = {};

  // Get missing required fields
  typedef.forEach(function (field) {
    var fieldName = field[0];
    var isRequired = field[1] === binformat.REQUIRED;

    if (isRequired && obj[fieldName] === undefined) {
      missingFields.push(fieldName);
    } else {
      fieldsMap[fieldName] = true;
    }
  });

  // Get fields that are not specified in format
  _Object$keys(obj).forEach(function (key) {
    if (!fieldsMap[key] && /^[A-Z]/.test(key)) {
      unknownFields.push(key);
    }
  });

  if (!(missingFields.length || unknownFields.length)) {
    // No missing or unknown fields
    return;
  }

  var errorMessage = undefined;

  if (obj.TransactionType !== undefined) {
    errorMessage = SerializedObject.lookup_type_tx(obj.TransactionType);
  } else if (obj.LedgerEntryType !== undefined) {
    errorMessage = SerializedObject.lookup_type_le(obj.LedgerEntryType);
  } else {
    errorMessage = 'TransactionMetaData';
  }

  if (missingFields.length > 0) {
    errorMessage += ' is missing fields: ' + JSON.stringify(missingFields);
  }
  if (unknownFields.length > 0) {
    errorMessage += (missingFields.length ? ' and' : '') + ' has unknown fields: ' + JSON.stringify(unknownFields);
  }

  throw new Error(errorMessage);
};

SerializedObject.prototype.parse_json = function (obj_) {
  // Create a copy of the object so we don't modify it
  var obj = extend(true, {}, obj_);
  var typedef = undefined;

  if (typeof obj.TransactionType === 'number') {
    obj.TransactionType = SerializedObject.lookup_type_tx(obj.TransactionType);
    if (!obj.TransactionType) {
      throw new Error('Transaction type ID is invalid.');
    }
  }

  if (typeof obj.LedgerEntryType === 'number') {
    obj.LedgerEntryType = SerializedObject.lookup_type_le(obj.LedgerEntryType);

    if (!obj.LedgerEntryType) {
      throw new Error('LedgerEntryType ID is invalid.');
    }
  }

  if (typeof obj.TransactionType === 'string') {
    typedef = binformat.tx[obj.TransactionType];
    if (!Array.isArray(typedef)) {
      throw new Error('Transaction type is invalid');
    }

    typedef = typedef.slice();
    obj.TransactionType = typedef.shift();
  } else if (typeof obj.LedgerEntryType === 'string') {
    typedef = binformat.ledger[obj.LedgerEntryType];

    if (!Array.isArray(typedef)) {
      throw new Error('LedgerEntryType is invalid');
    }

    typedef = typedef.slice();
    obj.LedgerEntryType = typedef.shift();
  } else if (typeof obj.AffectedNodes === 'object') {
    typedef = binformat.metadata;
  } else {
    throw new Error('Object to be serialized must contain either' + ' TransactionType, LedgerEntryType or AffectedNodes.');
  }

  SerializedObject.check_fields(typedef, obj);
  this.serialize(typedef, obj);
};

SerializedObject.prototype.append = function (bytes_) {
  var bytes = bytes_ instanceof SerializedObject ? bytes_.buffer : bytes_;

  // Make sure both buffer and bytes are Array. Either could be a Buffer.
  if (Array.isArray(this.buffer) && Array.isArray(bytes)) {
    // `this.buffer = this.buffer.concat(bytes)` can be unbearably slow for
    // large bytes length and acceptable bytes length is limited for
    // `Array.prototype.push.apply(this.buffer, bytes)` as every element in the
    // bytes array is pushed onto the stack, potentially causing a RangeError
    // exception. Both of these solutions are known to be problematic for
    // ledger 7501326. KISS instead

    for (var i = 0; i < bytes.length; i++) {
      this.buffer.push(bytes[i]);
    }
  } else {
    this.buffer = this.buffer.concat(bytes);
  }

  this.pointer += bytes.length;
};

SerializedObject.prototype.resetPointer = function () {
  this.pointer = 0;
};

function readOrPeek(advance) {
  return function (bytes) {
    var start = this.pointer;
    var end = start + bytes;

    if (end > this.buffer.length) {
      throw new Error('Buffer length exceeded');
    }

    var result = this.buffer.slice(start, end);

    if (advance) {
      this.pointer = end;
    }

    return result;
  };
}

SerializedObject.prototype.read = readOrPeek(true);

SerializedObject.prototype.peek = readOrPeek(false);

SerializedObject.prototype.to_bits = function () {
  return sjclcodec.bytes.toBits(this.buffer);
};

SerializedObject.prototype.to_hex = function () {
  return sjclcodec.hex.fromBits(this.to_bits()).toUpperCase();
};

SerializedObject.prototype.to_json = function () {
  var old_pointer = this.pointer;

  this.resetPointer();

  var output = {};

  while (this.pointer < this.buffer.length) {
    var key_and_value = stypes.parse(this);
    var key = key_and_value[0];
    var value = key_and_value[1];
    output[key] = SerializedObject.jsonify_structure(value, key);
  }

  this.pointer = old_pointer;

  return output;
};

SerializedObject.jsonify_structure = function (structure, fieldName) {
  var output = undefined;

  switch (typeof structure) {
    case 'number':
      switch (fieldName) {
        case 'LedgerEntryType':
          output = LEDGER_ENTRY_TYPES[structure];
          break;
        case 'TransactionResult':
          output = TRANSACTION_RESULTS[structure];
          break;
        case 'TransactionType':
          output = TRANSACTION_TYPES[structure];
          break;
        default:
          output = structure;
      }
      break;
    case 'object':
      if (structure === null) {
        break;
      }

      if (typeof structure.to_json === 'function') {
        output = structure.to_json();
      } else if (structure instanceof BN) {
        // We assume that any BN is a UInt64 field
        assert.equal(fieldType(fieldName), 'Int64');
        output = utils.arrayToHex(structure.toArray('bn', 8));
      } else {
        // new Array or Object
        output = new structure.constructor();

        var keys = _Object$keys(structure);

        for (var i = 0, l = keys.length; i < l; i++) {
          var key = keys[i];
          output[key] = SerializedObject.jsonify_structure(structure[key], key);
        }
      }
      break;
    default:
      output = structure;
  }

  return output;
};

SerializedObject.prototype.serialize = function (typedef, obj) {
  // Serialize object without end marker
  stypes.Object.serialize(this, obj, true);

  // ST: Old serialization
  /*
  // Ensure canonical order
  typedef = SerializedObject.sort_typedef(typedef);
   // Serialize fields
  for (let i=0, l=typedef.length; i<l; i++) {
    this.serialize_field(typedef[i], obj);
  }
  */
};

SerializedObject.prototype.hash = function (prefix) {
  var sign_buffer = new SerializedObject();

  // Add hashing prefix
  if (typeof prefix !== 'undefined') {
    stypes.Int32.serialize(sign_buffer, prefix);
  }

  // Copy buffer to temporary buffer
  sign_buffer.append(this.buffer);
  var bytes = hashjs.sha512().update(sign_buffer.buffer).digest();

  return UInt256.from_bytes(bytes.slice(0, 32));
};

// DEPRECATED
SerializedObject.prototype.signing_hash = SerializedObject.prototype.hash;

SerializedObject.prototype.serialize_field = function (spec, obj) {
  var name = spec[0];
  var presence = spec[1];

  if (typeof obj[name] !== 'undefined') {
    try {
      stypes.serialize(this, name, obj[name]);
    } catch (e) {
      // Add field name to message and rethrow
      e.message = 'Error serializing "' + name + '": ' + e.message;
      throw e;
    }
  } else if (presence === binformat.REQUIRED) {
    throw new Error('Missing required field ' + name);
  }
};

SerializedObject.get_field_header = function (type_id, field_id) {
  var buffer = [0];

  if (type_id > 0xF) {
    buffer.push(type_id & 0xFF);
  } else {
    buffer[0] += (type_id & 0xF) << 4;
  }

  if (field_id > 0xF) {
    buffer.push(field_id & 0xFF);
  } else {
    buffer[0] += field_id & 0xF;
  }

  return buffer;
};

SerializedObject.sort_typedef = function (typedef) {
  assert(Array.isArray(typedef));

  function sort_field_compare(a, b) {
    // Sort by type id first, then by field id
    return a[3] !== b[3] ? stypes[a[3]].id - stypes[b[3]].id : a[2] - b[2];
  }

  return typedef.sort(sort_field_compare);
};

SerializedObject.lookup_type_tx = function (id) {
  assert.strictEqual(typeof id, 'number');
  return TRANSACTION_TYPES[id];
};

SerializedObject.lookup_type_le = function (id) {
  assert(typeof id === 'number');
  return LEDGER_ENTRY_TYPES[id];
};

exports.SerializedObject = SerializedObject;