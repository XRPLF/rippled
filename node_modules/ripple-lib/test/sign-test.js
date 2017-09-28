'use strict';

const assert = require('assert');
const Seed = require('ripple-lib').Seed;

function _isNaN(n) {
  return typeof n === 'number' && isNaN(n);
}

describe('Signing', function() {
  describe('Keys', function() {
    it('SigningPubKey 1 (ripple-client issue #245)', function() {
      const seed = Seed.from_json('saESc82Vun7Ta5EJRzGJbrXb5HNYk');
      const key = seed.get_key('rBZ4j6MsoctipM6GEyHSjQKzXG3yambDnZ');
      const pub = key.pubKeyHex();
      assert.strictEqual(
        pub,
        '0396941B22791A448E5877A44CE98434DB217D6FB97D63F0DAD23BE49ED45173C9');
    });
    it('SigningPubKey 2 (master seed)', function() {
      const seed = Seed.from_json('snoPBrXtMeMyMHUVTgbuqAfg1SUTb');
      const key = seed.get_key('rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      const pub = key.pubKeyHex();
      assert.strictEqual(
         pub,
        '0330E7FC9D56BB25D6893BA3F317AE5BCF33B3291BD63DB32654A313222F7FD020');
    });
  });
  describe('parse_json', function() {
    it('empty string', function() {
      assert(_isNaN(new Seed().parse_json('').to_json()));
    });
    it('hex string', function() {
      // 32 0s is a valid hex repr of seed bytes
      const str = new Array(33).join('0');
      assert.strictEqual((new Seed().parse_json(str).to_json()),
                         'sp6JS7f14BuwFY8Mw6bTtLKWauoUs');
    });
    it('passphrase', function() {
      const str = new Array(60).join('0');
      assert.strictEqual('snFRPnVL3secohdpwSie8ANXdFQvG',
                         new Seed().parse_json(str).to_json());
    });
    it('null', function() {
      assert(_isNaN(new Seed().parse_json(null).to_json()));
    });
  });
  describe('parse_passphrase', function() {
    it('invalid passphrase', function() {
      assert.throws(function() {
        new Seed().parse_passphrase(null);
      });
    });
  });
  describe('get_key', function() {
    it('get key from invalid seed', function() {
      assert.throws(function() {
        new Seed().get_key('rBZ4j6MsoctipM6GEyHSjQKzXG3yambDnZ');
      });
    });
  });
});
