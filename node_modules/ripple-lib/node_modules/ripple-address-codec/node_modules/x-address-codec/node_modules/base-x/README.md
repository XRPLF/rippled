# base-x

[![TRAVIS](https://secure.travis-ci.org/cryptocoinjs/base-x.png)](http://travis-ci.org/cryptocoinjs/base-x)
[![NPM](http://img.shields.io/npm/v/base-x.svg)](https://www.npmjs.org/package/base-x)

[![js-standard-style](https://cdn.rawgit.com/feross/standard/master/badge.svg)](https://github.com/feross/standard)

Fast base encoding / decoding of any given alphabet using bitcoin style leading
zero compression.

## Example

Base58

``` javascript
var BASE58 = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'
var bs58 = require('base-x')(BASE58)

var decoded = bs58.decode('5Kd3NBUAdUnhyzenEwVLy9pBKxSwXvE9FMPyR4UKZvpe6E3AgLr')

console.log(decoded)
// => <Buffer 80 ed db dc 11 68 f1 da ea db d3 e4 4c 1e 3f 8f 5a 28 4c 20 29 f7 8a d2 6a f9 85 83 a4 99 de 5b 19>

console.log(bs58.encode(decoded))
// => 5Kd3NBUAdUnhyzenEwVLy9pBKxSwXvE9FMPyR4UKZvpe6E3AgLr
```

### Alphabets

See below for a list of commonly recognized alphabets, and their respective base.

Base | Alphabet
------------- | -------------
11 | `0123456789a`
16 | `0123456789abcdef`
32 | `0123456789ABCDEFGHJKMNPQRSTVWXYZ`
36 | `0123456789abcdefghijklmnopqrstuvwxyz`
58 | `123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz`
62 | `0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ`
64 | `ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/`
66 | `ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.!~`


## How it works

It encodes octet arrays by doing long divisions on all significant digits in the
array, creating a representation of that number in the new base. Then for every
leading zero in the input (not significant as a number) it will encode as a
single leader character. This is the first in the alphabet and will decode as 8
bits. The other characters depend upon the base. For example, a base58 alphabet
packs roughly 5.858 bits per character.

This means the encoded string 000f (using a 0-f alphabet) will actually decode
to 4 bytes unlike a typical hex codec which uniformly packs 4 bits into each
character.

While unusual, this does mean that no padding is required and it works for bases
like 43. If you need standard hex encoding or base64 encoding you probably don't
want this.

The algorithm used to convert the base of the number is roughly this:

```python
significant =  12345
base = 16
digits = []
while significant > base:
  significant, remainder = divmod(significant, base)
  digits.append(remainder)
digits.append(significant)
assert list(reversed(digits)) == [3,0,3,9]
assert hex(12345) == '0x3039'
```

Of course the input is actually an array of digits already :)

## License

This library is free and open-source software released under the MIT license.

