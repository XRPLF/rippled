var assert = require('./../lib/assert-diff');
var assertOrig = require('assert');

it('diff deep equal array', function() {
  assert.string('yea');
  assert.deepEqual([22, 'loi', {pow: true}], [22, 'lol', {pow: true}]);
})

it('original deep equal array', function() {
  assertOrig.deepEqual([22, 'loi', {pow: true}], [22, 'lol', {pow: true}]);
})

it('diff deep equal string', function() {
  assert.deepEqual('tomato', 'tomeito');
})

it('original deep equal string', function() {
  assertOrig.deepEqual('tomato', 'tomeito', 'hahaa');
})

it('diff deep equal', function() {
  assert.deepEqual({pow: 'boom', same: true, foo: 2}, {same: true, bar: 2, pow: 'bang'});
})

it('original deep equal', function() {
  assertOrig.deepEqual({pow: 'boom', same: true, foo: 2}, {same: true, bar: 2, pow: 'bang'});
})

it('diff deep equal with message', function() {
  assert.deepEqual({pow: 'boom', same: true, foo: 2}, {same: true, bar: 2, pow: 'bang'}, 'this should fail');
})

it('strict diff deep equal', function() {
  assert.deepEqual({a: 1}, {a: 1}, 'this should not fail');

  assert.options.strict = false;
  assert.deepEqual({a: 1, b: 2}, {a: true, b: '2'}, 'this should not fail');

  assert.options.strict = true;
  assert.deepEqual({a: 1, b: 2}, {a: true, b: '2'}, 'this should fail');
})
