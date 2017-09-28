'use strict';

var hashjs = require('hash.js');
var apiFactory = require('x-address-codec');

var NODE_PUBLIC = 28;
var NODE_PRIVATE = 32;
var ACCOUNT_ID = 0;
var FAMILY_SEED = 33;
var ED25519_SEED = [0x01, 0xE1, 0x4B];

module.exports = apiFactory({
  sha256: function(bytes) {
    return hashjs.sha256().update(bytes).digest();
  },
  defaultAlphabet: 'ripple',
  codecMethods: {
    EdSeed: {
      expectedLength: 16,
      version: ED25519_SEED
    },
    Seed: {
      // TODO: Use a map, not a parallel array
      versionTypes: ['ed25519', 'secp256k1'],
      versions: [ED25519_SEED, FAMILY_SEED],
      expectedLength: 16
    },
    AccountID: {version: ACCOUNT_ID},
    NodePublic: {version: NODE_PUBLIC},
    NodePrivate: {version: NODE_PRIVATE},
    K256Seed: {version: FAMILY_SEED}
  }
});
