# ripple-address-codec

[![NPM](https://img.shields.io/npm/v/ripple-address-codec.svg)](https://npmjs.org/package/ripple-address-codec) [![Build Status](https://img.shields.io/travis/sublimator/ripple-address-codec/master.svg)](https://travis-ci.org/sublimator/ripple-address-codec) [![Coverage Status](https://img.shields.io/coveralls/sublimator/ripple-address-codec/master.svg)](https://coveralls.io/github/sublimator/ripple-address-codec?branch=master)

## API

```js
> var api = require('ripple-address-codec');
> api.decodeSeed('sEdTM1uX8pu2do5XvTnutH6HsouMaM2')
{ version: [ 1, 225, 75 ],
  bytes: [ 76, 58, 29, 33, 63, 189, 251, 20, 199, 194, 141, 96, 148, 105, 179, 65 ],
  type: 'ed25519' }
> api.decodeSeed('sn259rEFXrQrWyx3Q7XneWcwV6dfL')
{ version: 33,
  bytes: [ 207, 45, 227, 120, 251, 221, 126, 46, 232, 125, 72, 109, 251, 90, 123, 255 ],
  type: 'secp256k1' }
> api.decodeAccountID('rJrRMgiRgrU6hDF4pgu5DXQdWyPbY35ErN')
[ 186,
  142,
  120,
  98,
  110,
  228,
  44,
  65,
  180,
  109,
  70,
  195,
  4,
  141,
  243,
  161,
  195,
  200,
  112,
  114 ]
```

## And ?? There's more to the wonderful world then ripple

We give you the kitchen sink.

```js
> console.log(api)
{ Codec: [Function: AddressCodec],
  codecs:
   { bitcoin:
      { alphabet: '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz',
        codec: [Object],
        base: 58 },
     ripple:
      { alphabet: 'rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz',
        codec: [Object],
        base: 58 },
     tipple:
      { alphabet: 'RPShNAF39wBUDnEGHJKLM4pQrsT7VWXYZ2bcdeCg65jkm8ofqi1tuvaxyz',
        codec: [Object],
        base: 58 },
     stellar:
      { alphabet: 'gsphnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCr65jkm8oFqi1tuvAxyz',
        codec: [Object],
        base: 58 } },
  decode: [Function: decode],
  encode: [Function: encode],
  decodeEdSeed: [Function],
  encodeEdSeed: [Function],
  decodeSeed: [Function],
  decodeAccountID: [Function],
  encodeAccountID: [Function],
  decodeNodePublic: [Function],
  encodeNodePublic: [Function],
  decodeNodePrivate: [Function],
  encodeNodePrivate: [Function],
  decodeK256Seed: [Function],
  encodeK256Seed: [Function] }
```

# Decode a bip32 bitcoin public key

```js
var pubVersion = [0x04, 0x88, 0xB2, 0x1E];
var options = {version: pubVersion, alphabet: 'bitcoin'};
var key = 'xpub661MyMwAqRbcEYS8w7XLSVeEsBXy79zSzH1J8vCdxAZningWLdN3zgtU6LBpB85b3D2yc8sfvZU521AAwdZafEz7mnzBBsz4wKY5e4cp9LB';
var decoded = api.decode(key, options);
var reencoded = api.encode(decoded, options);
console.log(key);
// 'xpub661MyMwAqRbcEYS8w7XLSVeEsBXy79zSzH1J8vCdxAZningWLdN3zgtU6LBpB85b3D2yc8sfvZU521AAwdZafEz7mnzBBsz4wKY5e4cp9LB'
console.log(reencoded);
// 'xpub661MyMwAqRbcEYS8w7XLSVeEsBXy79zSzH1J8vCdxAZningWLdN3zgtU6LBpB85b3D2yc8sfvZU521AAwdZafEz7mnzBBsz4wKY5e4cp9LB'
```