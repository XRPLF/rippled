/* eslint-disable max-len, valid-jsdoc */
'use strict';

const assert = require('assert');
const fs = require('fs');

const Ledger = require('ripple-lib').Ledger;

/**
* @param ledger_index {Number}
* Expects a corresponding ledger dump in $repo/test/fixtures/ folder
*/
function create_ledger_test(ledger_index) {
  describe(String(ledger_index), function() {
    const path = __dirname + '/fixtures/ledger-full-' + ledger_index + '.json';

    const ledger_raw = fs.readFileSync(path);
    const ledger_json = JSON.parse(ledger_raw);
    const ledger = Ledger.from_json(ledger_json);

    const hasAccounts = Array.isArray(ledger_json.accountState)
    && ledger_json.accountState.length > 0;

    if (hasAccounts) {
      it('has account_hash of ' + ledger_json.account_hash, function() {
        assert.equal(ledger_json.account_hash,
                     ledger.calc_account_hash({sanity_test: true}).to_hex());
      });
    }
    it('has transaction_hash of ' + ledger_json.transaction_hash, function() {
      assert.equal(ledger_json.transaction_hash,
        ledger.calc_tx_hash().to_hex());
    });
  });
}
describe('Ledger', function() {
  // This is the first recorded ledger with a non empty transaction set
  create_ledger_test(38129);
  // Because, why not.
  create_ledger_test(40000);
  // 1311 AffectedNodes, no accounts
  create_ledger_test(7501326);

  describe('#calcAccountRootEntryHash', function() {
    it('will calculate the AccountRoot entry hash for rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      const account = 'rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh';
      const expectedEntryHash = '2B6AC232AA4C4BE41BF49D2459FA4A0347E1B543A4C92FCEE0821C0201E2E9A8';
      const actualEntryHash = Ledger.calcAccountRootEntryHash(account);

      assert.equal(actualEntryHash.to_hex(), expectedEntryHash);
    });
  });

  describe('#calcRippleStateEntryHash', function() {
    it('will calculate the RippleState entry hash for rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh and rB5TihdPbKgMrkFqrqUC3yLdE8hhv4BdeY in USD', function() {
      const account1 = 'rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh';
      const account2 = 'rB5TihdPbKgMrkFqrqUC3yLdE8hhv4BdeY';
      const currency = 'USD';

      const expectedEntryHash = 'C683B5BB928F025F1E860D9D69D6C554C2202DE0D45877ADB3077DA4CB9E125C';
      const actualEntryHash1 = Ledger.calcRippleStateEntryHash(account1, account2, currency);
      const actualEntryHash2 = Ledger.calcRippleStateEntryHash(account2, account1, currency);

      assert.equal(actualEntryHash1.to_hex(), expectedEntryHash);
      assert.equal(actualEntryHash2.to_hex(), expectedEntryHash);
    });

    it('will calculate the RippleState entry hash for r3kmLJN5D28dHuH8vZNUZpMC43pEHpaocV and rUAMuQTfVhbfqUDuro7zzy4jj4Wq57MPTj in UAM', function() {
      const account1 = 'r3kmLJN5D28dHuH8vZNUZpMC43pEHpaocV';
      const account2 = 'rUAMuQTfVhbfqUDuro7zzy4jj4Wq57MPTj';
      const currency = 'UAM';

      const expectedEntryHash = 'AE9ADDC584358E5847ADFC971834E471436FC3E9DE6EA1773DF49F419DC0F65E';
      const actualEntryHash1 = Ledger.calcRippleStateEntryHash(account1, account2, currency);
      const actualEntryHash2 = Ledger.calcRippleStateEntryHash(account2, account1, currency);

      assert.equal(actualEntryHash1.to_hex(), expectedEntryHash);
      assert.equal(actualEntryHash2.to_hex(), expectedEntryHash);
    });
  });

  describe('#calcOfferEntryHash', function() {
    it('will calculate the Offer entry hash for r32UufnaCGL82HubijgJGDmdE5hac7ZvLw, sequence 137', function() {
      const account = 'r32UufnaCGL82HubijgJGDmdE5hac7ZvLw';
      const sequence = 137;
      const expectedEntryHash = '03F0AED09DEEE74CEF85CD57A0429D6113507CF759C597BABB4ADB752F734CE3';
      const actualEntryHash = Ledger.calcOfferEntryHash(account, sequence);

      assert.equal(actualEntryHash.to_hex(), expectedEntryHash);
    });
  });
});

// vim:sw=2:sts=2:ts=8:et
