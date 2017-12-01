# ripple-keypairs [![NPM](https://img.shields.io/npm/v/ripple-keypairs.svg)](https://npmjs.org/package/ripple-keypairs) [![Build Status](https://img.shields.io/travis/sublimator/ripple-keypairs/master.svg)](https://travis-ci.org/sublimator/ripple-keypairs) [![Coverage Status](https://coveralls.io/repos/sublimator/ripple-keypairs/badge.svg?branch=master&service=github)](https://coveralls.io/github/sublimator/ripple-keypairs?branch=master)

An implementation of ripple keypairs & wallet generation using
[elliptic](https://github.com/indutny/elliptic) which supports rfc6979 and
eddsa deterministic signatures.

## Generate a random wallet
```js
> var generateWallet = require('ripple-keypairs').generateWallet;
> generateWallet({type: 'ed25519'});
{ seed: 'sEd7t79mzn2dwy3vvpvRmaaLbLhvme6',
  accountID: 'r9LqNeG6qHxjeUocjvVki2XR35weJ9mZgQ',
  publicKey: 'ED5F5AC8B98974A3CA843326D9B88CEBD0560177B973EE0B149F782CFAA06DC66A' }
```

## Derive a wallet from a seed
```js
> var walletFromSeed = require('ripple-keypairs').walletFromSeed;
> walletFromSeed('sEd7t79mzn2dwy3vvpvRmaaLbLhvme6');
{ seed: 'sEd7t79mzn2dwy3vvpvRmaaLbLhvme6',
  accountID: 'r9LqNeG6qHxjeUocjvVki2XR35weJ9mZgQ',
  publicKey: 'ED5F5AC8B98974A3CA843326D9B88CEBD0560177B973EE0B149F782CFAA06DC66A' }')
```

## Generate random validator keys
```js
> var generateValidatorKeys = require('ripple-keypairs').generateValidatorKeys;
> generateValidatorKeys();
{ seed: 'ssC7Y9LMKhuzFMVueaj2fnTuGLftA',
  publicKey: 'n9MU2RsULUayZnWeLssjbMzVRPeVUUMgiPYTwe8eMgpdGDWp5t8C' }
```

## Derive validator keys from a seed
```js
> var validatorKeysFromSeed = require('ripple-keypairs').validatorKeysFromSeed;
> validatorKeysFromSeed('ssC7Y9LMKhuzFMVueaj2fnTuGLftA');
{ seed: 'ssC7Y9LMKhuzFMVueaj2fnTuGLftA',
  publicKey: 'n9MU2RsULUayZnWeLssjbMzVRPeVUUMgiPYTwe8eMgpdGDWp5t8C' }
```

## Derive accountID matching a validator public key (aka public generator)
```js
> var nodePublicAccountID = require('ripple-keypairs').nodePublicAccountID;
> nodePublicAccountID('n9MXXueo837zYH36DvMc13BwHcqtfAWNJY5czWVbp7uYTj7x17TH')
'rhcfR9Cg98qCxHpCcPBmMonbDBXo84wyTn'
```

## Sign a transaction
see [examples/sign-transaction.js](examples/sign-transaction.js)
```js
var Transaction = require('ripple-lib').Transaction;
var keyPairFromSeed = require('../').keyPairFromSeed;

var SIGNING_PREFIX = [0x53, 0x54, 0x58, 0x00];

function prettyJSON(obj) {
  return JSON.stringify(obj, undefined, 2);
}

function signingData(tx) {
  return SIGNING_PREFIX.concat(tx.serialize().buffer);
}

function signTxJson(seed, json) {
  var keyPair = keyPairFromSeed(seed);
  var tx = Transaction.from_json(json);
  var tx_json = tx.tx_json;

  tx_json.SigningPubKey = keyPair.pubKeyHex();
  tx_json.TxnSignature = keyPair.signHex(signingData(tx));

  var serialized = tx.serialize();

  var id = tx.hash('HASH_TX_ID', /* Uint256: */ false , /* pre: */ serialized);

  return {
    hash: id,
    tx_blob: serialized.to_hex(),
    tx_json: tx_json
  };
}

var seed = 'sEd7t79mzn2dwy3vvpvRmaaLbLhvme6';
var tx_json = {
  Account: 'r9LqNeG6qHxjeUocjvVki2XR35weJ9mZgQ',
  Amount: '1000',
  Destination: 'rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh',
  Fee: '10',
  Flags: 2147483648,
  Sequence: 1,
  TransactionType: 'Payment',
};

console.log(prettyJSON(signTxJson(seed, tx_json)));

```

```json
{
  "hash": "1B6B9652F95D826C9D9C3FD30F208130433CBC7C48C10F6EC2CC5E4A85D167FF",
  "tx_blob": "120000228000000024000000016140000000000003E868400000000000000A7321ED5F5AC8B98974A3CA843326D9B88CEBD0560177B973EE0B149F782CFAA06DC66A74407D0825105229923B261C716F225181E5A66A34C9480446ABE64818A673954CC34D42946CD82172814F037976AE3800BDE983624A45FCDBED4A548C4650BF900D81145B812C9D57731E27A2DA8B1830195F88EF32A3B68314B5F762798A53D543A014CAF8B297CFF8F2F937E8",
  "tx_json": {
    "Account": "r9LqNeG6qHxjeUocjvVki2XR35weJ9mZgQ",
    "Amount": "1000",
    "Destination": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "Fee": "10",
    "Flags": 2147483648,
    "Sequence": 1,
    "TransactionType": "Payment",
    "SigningPubKey": "ED5F5AC8B98974A3CA843326D9B88CEBD0560177B973EE0B149F782CFAA06DC66A",
    "TxnSignature": "7D0825105229923B261C716F225181E5A66A34C9480446ABE64818A673954CC34D42946CD82172814F037976AE3800BDE983624A45FCDBED4A548C4650BF900D"
  }
}
```
