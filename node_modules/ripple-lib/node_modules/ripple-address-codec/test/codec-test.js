'use strict';

var assert = require('assert');
var api = require('../');

function toHex(bytes) {
  return new Buffer(bytes).toString('hex').toUpperCase();
}

function toBytes(hex) {
  return new Buffer(hex, 'hex').toJSON().data;
}

describe('ripple-address-codec', function() {
  function makeTest(type, base58, hex, opts) {
    opts = opts || {};

    it('can translate between ' + hex + ' and ' + base58, function() {
      var actual = api['encode' + type](toBytes(hex));
      assert.equal(actual, base58);
    });
    it('can translate between ' + base58 + ' and ' + hex, function() {
      var buf = api['decode' + type](base58);
      assert.equal(toHex(buf), hex);
    });
  }

  makeTest('AccountID', 'rJrRMgiRgrU6hDF4pgu5DXQdWyPbY35ErN',
                        'BA8E78626EE42C41B46D46C3048DF3A1C3C87072');

  makeTest(
    'NodePublic',
    'n9MXXueo837zYH36DvMc13BwHcqtfAWNJY5czWVbp7uYTj7x17TH',
    '0388E5BA87A000CB807240DF8C848EB0B5FFA5C8E5A521BC8E105C0F0A44217828');

  makeTest('K256Seed', 'sn259rEFXrQrWyx3Q7XneWcwV6dfL',
                       'CF2DE378FBDD7E2EE87D486DFB5A7BFF');

  makeTest('EdSeed', 'sEdTM1uX8pu2do5XvTnutH6HsouMaM2',
                     '4C3A1D213FBDFB14C7C28D609469B341');

  it('can decode arbitray seeds', function() {
    var decoded = api.decodeSeed('sEdTM1uX8pu2do5XvTnutH6HsouMaM2');
    assert.equal(toHex(decoded.bytes), '4C3A1D213FBDFB14C7C28D609469B341');
    assert.equal(decoded.type, 'ed25519');

    var decoded2 = api.decodeSeed('sn259rEFXrQrWyx3Q7XneWcwV6dfL');
    assert.equal(toHex(decoded2.bytes), 'CF2DE378FBDD7E2EE87D486DFB5A7BFF');
    assert.equal(decoded2.type, 'secp256k1');
  });

  it('can pass a type as second arg to encodeSeed', function() {
    var edSeed = 'sEdTM1uX8pu2do5XvTnutH6HsouMaM2';
    var decoded = api.decodeSeed(edSeed);
    assert.equal(toHex(decoded.bytes), '4C3A1D213FBDFB14C7C28D609469B341');
    assert.equal(decoded.type, 'ed25519');
    assert.equal(api.encodeSeed(decoded.bytes, decoded.type), edSeed);
  });
});
