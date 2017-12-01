
'use strict';
var BigNumber = require('bignumber.js');
var AccountFields = require('./utils').constants.AccountFields;

function parseField(info, value) {
  if (info.encoding === 'hex' && !info.length) {
    return new Buffer(value, 'hex').toString('ascii');
  }
  if (info.shift) {
    return new BigNumber(value).shift(-info.shift).toNumber();
  }
  return value;
}

function parseFields(data) {
  var settings = {};
  for (var fieldName in AccountFields) {
    var fieldValue = data[fieldName];
    if (fieldValue !== undefined) {
      var info = AccountFields[fieldName];
      settings[info.name] = parseField(info, fieldValue);
    }
  }
  return settings;
}

module.exports = parseFields;