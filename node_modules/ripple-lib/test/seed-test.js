/* eslint max-len: 0 */
/* eslint-disable max-nested-callbacks */
'use strict';
const assert = require('assert');
const Seed = require('ripple-lib').Seed;

describe('Seed', function() {
  it('saESc82Vun7Ta5EJRzGJbrXb5HNYk', function() {
    const seed = Seed.from_json('saESc82Vun7Ta5EJRzGJbrXb5HNYk');
    assert.strictEqual(seed.to_hex(), 'FF1CF838D02B2CF7B45BAC27F5F24F4F');
  });
  it('can create ed25519 seeds from a phrase', function() {
    const seed = Seed.from_json('phrase').set_ed25519().to_json();
    assert.strictEqual(seed, 'sEdT7U4WpkoiH6wBoNeLzDi1eu9N64Y');
  });
  it('sp6iDHnmiPN7tQFHm5sCW59ax3hfE', function() {
    const seed = Seed.from_json('sp6iDHnmiPN7tQFHm5sCW59ax3hfE');
    assert.strictEqual(seed.to_hex(), '00AD8DA764C3C8AF5F9B8D51C94B9E49');
  });
  it('sp6iDHnmiPN7tQFHm5sCW59ax3hfE using parse_base58', function() {
    const seed = new Seed().parse_base58('sp6iDHnmiPN7tQFHm5sCW59ax3hfE');
    assert.strictEqual(seed.to_hex(), '00AD8DA764C3C8AF5F9B8D51C94B9E49');
  });
  it('parse_base58 should throw on non-string input', function() {
    assert.throws(function() {
      new Seed().parse_base58(1);
    });
  });
  it('parse_base58 should make invalid seed from empty string', function() {
    const seed = new Seed().parse_base58('');
    assert(!seed.is_valid());
  });
  it('parse_base58 should make invalid seed from invalid input', function() {
    const seed = new Seed().parse_base58('Xs');
    assert(!seed.is_valid());
  });
  it('should return the key_pair for a valid account and secret pair', function() {
    const address = 'r3GgMwvgvP8h4yVWvjH1dPZNvC37TjzBBE';
    const seed = Seed.from_json('shsWGZcmZz6YsWWmcnpfr6fLTdtFV');
    const keyPair = seed.get_key();
    assert.strictEqual(keyPair.accountID(), address);
    assert.strictEqual(keyPair.pubKeyHex(), '02F89EAEC7667B30F33D0687BBA86C3FE2A08CCA40A9186C5BDE2DAA6FA97A37D8');
  });
});

// vim:sw=2:sts=2:ts=8:et
