'use strict';
var assert = require('assert');
var convertBase = require('ripple-lib').convertBase;

// Test cases from RFC-1924 (a joke RFC)
var BASE85 = ('0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'
              + 'abcdefghijklmnopqrstuvwxyz!#$%&()*+-;<=>?@^_`{|}~');
var BASE10 = BASE85.slice(0, 10);
var BASE16 = BASE85.slice(0, 16);

var DATA16 = '108000000000000000080800200C417A';
var DATA10 = '21932261930451111902915077091070067066';
var DATA85 = '4)+k&C#VzJ4br>0wv%Yp';

function encode(digitArray, encoding) {
  return digitArray.map(function(i) {
    return encoding.charAt(i);
  }).join('');
}

function decode(encoded, encoding) {
  return encoded.split('').map(function(c) {
    return encoding.indexOf(c);
  });
}

function convertBaseEncoded(value, fromEncoding, toEncoding) {
  var digitArray = decode(value, fromEncoding);
  var converted = convertBase(digitArray, fromEncoding.length,
    toEncoding.length);
  return encode(converted, toEncoding);
}

describe('convertBase', function() {
  it('DEC -> HEX', function () {
    assert.strictEqual(convertBaseEncoded(DATA10, BASE10, BASE16), DATA16);
  });
  it('HEX -> DEC', function () {
    assert.strictEqual(convertBaseEncoded(DATA16, BASE16, BASE10), DATA10);
  });
  it('DEC -> B85', function () {
    assert.strictEqual(convertBaseEncoded(DATA10, BASE10, BASE85), DATA85);
  });
  it('HEX -> B85', function () {
    assert.strictEqual(convertBaseEncoded(DATA16, BASE16, BASE85), DATA85);
  });
  it('B85 -> DEC', function () {
    assert.strictEqual(convertBaseEncoded(DATA85, BASE85, BASE10), DATA10);
  });
  it('B85 -> HEX', function () {
    assert.strictEqual(convertBaseEncoded(DATA85, BASE85, BASE16), DATA16);
  });
});
