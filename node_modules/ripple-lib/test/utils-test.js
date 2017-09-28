var assert = require('assert');
var utils  = require('ripple-lib').utils;

describe('Utils', function() {
  describe('hexToString and stringToHex', function() {
    it('Even: 123456', function () {
      assert.strictEqual('123456', utils.stringToHex(utils.hexToString('123456')));
    });
    it('Odd: 12345', function () {
      assert.strictEqual('012345', utils.stringToHex(utils.hexToString('12345')));
    });
    it('Under 10: 0', function () {
      assert.strictEqual('00', utils.stringToHex(utils.hexToString('0')));
    });
    it('Under 10: 1', function () {
      assert.strictEqual('01', utils.stringToHex(utils.hexToString('1')));
    });
    it('Empty', function () {
      assert.strictEqual('', utils.stringToHex(utils.hexToString('')));
    });
  });
});

// vim:sw=2:sts=2:ts=8:et
