# x-address-codec

[![NPM](https://img.shields.io/npm/v/x-address-codec.svg)](https://npmjs.org/package/x-address-codec) [![Build Status](https://img.shields.io/travis/sublimator/x-address-codec/master.svg)](https://travis-ci.org/sublimator/x-address-codec) [![Coverage Status](https://img.shields.io/coveralls/sublimator/x-address-codec/master.svg)](https://coveralls.io/github/sublimator/x-address-codec?branch=master)

This is a meta package, that exposes an api factory. It's really not as boring
as it sounds. We only ask you bring your own hash
([create-hash](https://www.npmjs.com/package/create-hash),
 [crypto](https://nodejs.org/api/crypto.html)) to the party, as we already
provide a free [base-x](https://github.com/dcousens/base-x) codec for your
heavy lifting pleasure.

# What, what? This does what exactly ?

At the party, mostly people just stand around and encode/decode crypto coin
address strings to bytes and back. Thrilling right?

# Alphabet Soup

We currently serve these alphabets. Make a pull request if you'd like to add one
to the menu.

* ripple
* tipple
* bitcoin
* stellar

# API

```js
var apiFactory = require('../');
var createHash = require('create-hash');

var api = apiFactory({
  // We probably have your favorite alphabet, if not, contact us
  defaultAlphabet: 'stellar',
  // But we insist you bring your own hash to the party :)
  sha256: function(bytes) {
    return createHash('sha256').update(new Buffer(bytes)).digest();
  },
  // We'll endow your api with encode|decode* for you
  codecMethods : {
    // public keys
    AccountID : {version: 0x00},
    // secrets
    Seed: {version: 0x21}
  },
  // Why the hell don't we just export these versions too?
  // Err.. Shutup :) We're getting to it.
});

var buf = new Buffer("00000000000000000000000000000000", 'hex');
// It can encode a Buffer
var encoded = api.encodeSeed(buf);
// It returns Array<Number>
var decoded = api.decodeSeed(encoded);
// It can of course encode an Array<Number> too
var reencoded = api.encodeSeed(decoded)

console.log(encoded);
console.log(reencoded);
// ps6JS7f14BuwFY8Mw6bTtLKWauoUp
// ps6JS7f14BuwFY8Mw6bTtLKWauoUp

console.log(decoded);
// [ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 ]
```

## We could actually encode the seed as a ripple one if we chose :)

```js
console.log(api.encode(decoded, {alphabet: 'ripple', version: 33}));
// sp6JS7f14BuwFY8Mw6bTtLKWauoUs
```

## Wait, what if we wanted to create a prefix for the new nifty spaceMan secrets?
```js
var prefix = api.codecs.stellar.findPrefix(16 /* bytes */, 'spaceMan');
var spacey = api.encode(decoded, {version: prefix});
console.log(spacey);
// spaceMan7qBfYEUBHSWDsZjJHctnNQi2pCTn
console.log(api.decode(spacey, {version: prefix}));
// [ 0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0 ]
```

## You may as well make a little mini module, and export it :)

```js
module.exports = api;
```

## Hell, you could even npm publish it :)
```bash
$ npm publish
```

## Anway, what is actually exported here?
```js
console.log(api)
/*
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
  decodeAccountID: [Function],
  encodeAccountID: [Function],
  decodeSeed: [Function],
  encodeSeed: [Function] }
*/
```
