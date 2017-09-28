'use strict';

const codec = require('ripple-address-codec');
const assert = require('assert-diff');
const utils = require('../src/utils');
const keypairs = require('../src');
const _ = require('lodash');

const {
  KeyType,
  K256Pair,
  seedFromPhrase,
  Ed25519Pair,
  keyPairFromSeed,
  generateWallet,
  walletFromSeed,
  walletFromPhrase,
  generateValidatorKeys,
  validatorKeysFromSeed,
  validatorKeysFromPhrase,
  nodePublicAccountID
} = keypairs;

const {SerializedObject} = require('ripple-lib');
const TX_HASH_PREFIX_SIGN = [0x53, 0x54, 0x58, 0x00];

const FIXTURES = {
  message: [0xB, 0xE, 0xE, 0xF],

  tx_json: {
    Account: 'rJZdUusLDtY9NEsGea7ijqhVrXv98rYBYN',
    Amount: '1000',
    Destination: 'rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh',
    Fee: '10',
    Flags: 2147483648,
    Sequence: 1,
    SigningPubKey: 'EDD3993CDC6647896C455F136648B7750' +
                   '723B011475547AF60691AA3D7438E021D',
    TransactionType: 'Payment',
    expected_sig: 'C3646313B08EED6AF4392261A31B961F' +
                  '10C66CB733DB7F6CD9EAB079857834C8' +
                  'B0334270A2C037E63CDCCC1932E08328' +
                  '82B7B7066ECD2FAEDEB4A83DF8AE6303'
  }
};

describe('ED25519Pair', function() {
  let pair;

  before(function() {
    pair = Ed25519Pair.fromSeed(seedFromPhrase('niq'));
  });

  it('can be constructed from a pulbic key to verify a txn', function() {
    const sig = pair.sign(FIXTURES.message);
    const key = Ed25519Pair.fromPublic(pair.pubKeyCanonicalBytes());
    assert(key.verify(FIXTURES.message, sig));
    assert(!key.verify(FIXTURES.message.concat(0), sig));
  });

  it('has a String member `type` equal to KeyPair.ed25519 constant',
      function() {
    assert.equal(pair.type, KeyType.ed25519);
  });

  it('has a public key representation beginning with ED', function() {
    const pub_hex = pair.pubKeyHex();
    assert(pub_hex.length === 66);
    assert(pub_hex.slice(0, 2) === 'ED');
  });
  it('derives the same keypair for a given passphrase as rippled', function() {
    const pub_hex = pair.pubKeyHex();
    const target_hex = 'EDD3993CDC6647896C455F136648B7750' +
                   '723B011475547AF60691AA3D7438E021D';
    assert.equal(pub_hex, target_hex);
  });
  it('generates the same account_id as rippled for a given keypair',
      function() {
    assert.equal(pair.accountID(),
                 'rJZdUusLDtY9NEsGea7ijqhVrXv98rYBYN');
  });
  it('creates signatures that are a function of secret/message', function() {
    const signature = pair.sign(FIXTURES.message);
    assert(Array.isArray(signature));
    assert(pair.verify(FIXTURES.message, signature));
  });
  it('signs transactions exactly as rippled', function() {
    const so = SerializedObject.from_json(FIXTURES.tx_json);
    const message = TX_HASH_PREFIX_SIGN.concat(so.buffer);
    const sig = pair.signHex(message);
    assert.equal(sig, FIXTURES.tx_json.expected_sig);
  });
});

describe('keyPairFromSeed', function() {
  it('returns an Ed25519Pair from an ed25519 seed', function() {
    const pair = keyPairFromSeed('sEdTM1uX8pu2do5XvTnutH6HsouMaM2');
    assert(pair instanceof Ed25519Pair);
  });
  it('returns a K256Pair from an secp256k1 (default) seed', function() {
    const pair = keyPairFromSeed('sn259rEFXrQrWyx3Q7XneWcwV6dfL');
    assert(pair instanceof K256Pair);
  });
  it('can be intantiated with an array of bytes', function() {
    const seed = 'sn259rEFXrQrWyx3Q7XneWcwV6dfL';
    const {bytes} = codec.decodeSeed(seed);
    const pair = keyPairFromSeed(bytes);
    assert(pair instanceof K256Pair);
    assert.equal(pair.seed(), seed);
  });
});

describe('walletFromPhrase', function() {
  it('can gan generate ed25519 wallets', function() {
    const expected = {
      seed: 'sEd7rBGm5kxzauRTAV2hbsNz7N45X91',
      accountID: 'rJZdUusLDtY9NEsGea7ijqhVrXv98rYBYN',
      publicKey:
        'ED' +
        'D3993CDC6647896C455F136648B7750723B011475547AF60691AA3D7438E021D'
    };
    const wallet = walletFromPhrase('niq', 'ed25519');
    assert.deepEqual(wallet, expected);
  });
  it('generates secp256k1 wallets by default', function() {
    const expected = {
      seed: 'shQUG1pmPYrcnSUGeuJFJTA1b3JSL',
      accountID: 'rNvfq2SVbCiio1zkN5WwLQW8CHgy2dUoQi',
      publicKey:
        '02' +
        '1E788CDEB9104C9179C3869250A89999C1AFF92D2C3FF7925A1696835EA3D840'
    };
    const wallet = walletFromPhrase('niq');
    assert.deepEqual(wallet, expected);
  });
});

describe('validatorKeysFromPhrase', function() {
  it('generates keys used by peer nodes/validators', function() {
    const expected = {
      seed: 'shQUG1pmPYrcnSUGeuJFJTA1b3JSL',
      publicKey: 'n9KNees3ippJvi7ZT1GqHMCmEmmkCVPxQRPfU5tPzmg9MtWevpjP'
    };
    const wallet = validatorKeysFromPhrase('niq');
    assert.deepEqual(wallet, expected);
  });
});

describe('generateWallet', function() {
  function random(len) {
    return _.fill(Array(len), 0);
  }

  it('can generate ed25519 wallets', function() {
    const expected = {
      seed: 'sEdSJHS4oiAdz7w2X2ni1gFiqtbJHqE',
      accountID: 'r9zRhGr7b6xPekLvT6wP4qNdWMryaumZS7',
      publicKey:
        'ED' +
        '1A7C082846CFF58FF9A892BA4BA2593151CCF1DBA59F37714CC9ED39824AF85F'
    };
    const actual = generateWallet({type: 'ed25519', random});
    assert.deepEqual(actual, expected);
    assert.deepEqual(walletFromSeed(actual.seed), expected);
  });
  it('can generate secp256k1 wallets (by default)', function() {
    const expected = {
      seed: 'sp6JS7f14BuwFY8Mw6bTtLKWauoUs',
      accountID: 'rGCkuB7PBr5tNy68tPEABEtcdno4hE6Y7f',
      publicKey:
        '03' +
        '90A196799EE412284A5D80BF78C3E84CBB80E1437A0AECD9ADF94D7FEAAFA284'
    };
    const actual = generateWallet({type: undefined, random});
    assert.deepEqual(actual, expected);
    assert.deepEqual(walletFromSeed(actual.seed), expected);
  });
});

describe('generateValidatorKeys', function() {
  function random(len) {
    return _.fill(Array(len), 0);
  }
  it('can generate secp256k1 validator keys', function() {
    /*
    rippled validation_create 00000000000000000000000000000000
    {
       "result" : {
          "status" : "success",
          "validation_key" : "A A A A A A A A A A A A",
          "validation_public_key" :
              "n9LPxYzbDpWBZ1bC3J3Fdkgqoa3FEhVKCnS8yKp7RFQFwuvd8Q2c",
          "validation_seed" : "sp6JS7f14BuwFY8Mw6bTtLKWauoUs"
       }
    }
    */
    const expected = {
      seed: 'sp6JS7f14BuwFY8Mw6bTtLKWauoUs',
      publicKey: 'n9LPxYzbDpWBZ1bC3J3Fdkgqoa3FEhVKCnS8yKp7RFQFwuvd8Q2c'
    };
    const actual = generateValidatorKeys({random});
    assert.deepEqual(actual, expected);
    assert.deepEqual(validatorKeysFromSeed(actual.seed), expected);
  });

  it('can generate the correct accountID from validator public key', () => {
    const accountID = 'rhcfR9Cg98qCxHpCcPBmMonbDBXo84wyTn';
    const validatorPublic =
          'n9MXXueo837zYH36DvMc13BwHcqtfAWNJY5czWVbp7uYTj7x17TH';
    assert.equal(nodePublicAccountID(validatorPublic), accountID);
  });
});

describe('K256Pair', function() {
  describe('generated tests', function() {
    /* eslint-disable max-len */
    const expected = [
      '30440220312B2E0894B81A2E070ACE566C5DFC70CDD18E67D44E2CFEF2EB5495F7DE2DAC02205E155C0019502948C265209DFDD7D84C4A05BD2C38CEE6ECD7C33E9C9B12BEC2',
      '304402202A5860A12C15EBB8E91AA83F8E19D85D4AC05B272FC0C4083519339A7A76F2B802200852F9889E1284CF407DC7F73D646E62044C5AB432EAEF3FFF3F6F8EE9A0F24C',
      '3045022100B1658C88D1860D9F8BEB25B79B3E5137BBC2C382D08FE7A068FFC6AB8978C8040220644F64B97EA144EE7D5CCB71C2372DD730FA0A659E4C18241A80D6C915350263',
      '3045022100F3E541330FF79FFC42EB0491EDE1E47106D94ECFE3CDB2D9DD3BC0E8861F6D45022013F62942DD626D6C9731E317F372EC5C1F72885C4727FDBEE9D9321BC530D7B2',
      '3045022100998ABE378F4119D8BEE9843482C09F0D5CE5C6012921548182454C610C57A269022036BD8EB71235C4B2C67339DE6A59746B1F7E5975987B7AB99B313D124A69BB9F'
    ];
    /* eslint-enable max-len */
    const key = K256Pair.fromSeed(seedFromPhrase('niq'));
    function test_factory(i) {
      it('can deterministically sign/verify message [' + i + ']', function() {
        const message = [i];
        const sig = key.sign(message);
        assert.equal(utils.bytesToHex(sig), expected[i]);
        assert(key.verify(message, sig));
      });
    }

    for (let n = 0; n < 5; n++) {
      test_factory(n);
    }
  });
});
