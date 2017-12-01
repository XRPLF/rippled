var assert = require('./../lib/assert-diff')
var assertOrig = require('assert')

it('assert() works on both', function() {
  assertOrig(true)
  try {
    assertOrig(false)
    assertOrig.fail('Should fail on false')
  } catch (e) {
    assertOrig.equal(e.actual, false)
  }
  assert(true)
  try {
    assert(false)
    assert.fail('Should fail on false')
  } catch (e) {
    assert.equal(e.actual, false)
  }
})

it('strict diff deep equal', function() {
  assert.deepEqual({a: 1}, {a: 1}, 'this should not fail')
  assert.deepEqual({a: 1, b: 2}, {a: true, b: '2'}, 'this should not fail')

  assert.options.strict = true
  assertOrig.deepEqual({a: 1, b: 2}, {a: true, b: '2'}, 'this should not fail')
  try {
    assert.deepEqual({a: 1, b: 2}, {a: true, b: '2'}, 'this should fail')
    assert.fail('Should fail on false')
  } catch (e) {
    assert.notEqual(e.actual, 'Should fail on false')
  }
})
