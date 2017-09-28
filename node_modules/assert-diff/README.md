Drop-in replacement for assert to give colored diff on command-line with deepEqual.

This exists to give better diff on error when comparing objects or arrays with Mocha.

Should work on *nix shells.

Also has more asserts from https://npmjs.org/package/assert-plus

## Usage ##
```javascript
var assert = require('assert-diff')

it('diff deep equal with message', function() {
  assert.deepEqual({pow: "boom", same: true, foo: 2}, {same: true, bar: 2, pow: "bang"}, "this should fail")
})
```
Should give you:

![](https://raw.github.com/pihvi/assert-diff/master/test/example.png)

## Strict mode ##
By default everything should work as with Node's deepEqual. Using strict mode is probably what you want. At least is for me.
The following example will pass with Node's deepEqual but will fail using strict mode:
```javascript
var assert = require('assert-diff')
assert.options.strict = true

it('strict diff deep equal', function() {
  assert.deepEqual({a: 1, b: 2}, {a: true, b: "2"}, "this should fail")
})
```
Should give you:

![](https://raw.github.com/pihvi/assert-diff/master/test/example2.png)

## Release notes ##

###  1.0.1 Feb 18, 2015  ###
- Default behaviour back to non strict to be drop-in replacement for Node assert

###  1.0.0 Feb 18, 2015  ###
- Support assert in constructor e.g. assert(true)

###  0.0.x before 2015 ###
- Initial implementation

## License ##
Apache 2.0
