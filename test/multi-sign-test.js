let assert    = require('assert');
let _         = require('lodash');
let async     = require('async');
let testutils = require('./testutils');
let config    = testutils.init_config();
let accounts  = require('./testconfig').accounts;
let Amount    = require('ripple-lib').Amount;
let Transaction    = require('ripple-lib').Transaction;

suite('MultiSign', function() {
  let $ = {};
  let opts = {};

  setup(function(done) {
    testutils.build_setup(opts).call($, done);
  });

  setup(function(done) {
    $.remote.local_signing = false;
    testutils.create_accounts(
      $.remote,
      'root',
      Amount.from_human('1000 XRP'),
      ['alice', 'bob', 'carol'],
      done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  function getAliceSequence(callback) {
    return $.remote.account('alice')._transactionManager._nextSequence;
  }

  test('remote signing', function(done) {
    async.series([
    function(callback) {
      let tx = $.remote.createTransaction('SignerListSet', {
        account: accounts.alice.account,
        signers: [
          {account: accounts.bob.account, weight: 1},
          {account: accounts.carol.account, weight: 2 }
        ],
        signerQuorum: 3
      });

      testutils.submit_transaction(tx, callback);
    },

    function(callback) {
      let tx = $.remote.createTransaction('AccountSet', {account: accounts.alice.account});
      tx.setFee(100);
      tx.setSequence(getAliceSequence());
      tx.setLastLedgerSequenceOffset(5);

      let mTx = Transaction.from_json(tx.getMultiSigningJson());

      [accounts.bob, accounts.carol].forEach(account => {
        let signer = mTx.multiSign( account.account, account.secret);
        assert(signer.Account);
        assert(signer.SigningPubKey);
        assert(signer.TxnSignature);

        tx.addMultiSigner(signer);
      });

      tx.once('submitted', function(res) {
        assert.strictEqual(res.engine_result, 'tesSUCCESS');
        assert.deepEqual(res.tx_json.Signers, tx.getMultiSigners());
        callback();
      });

      tx.submit();
    }
    ], done);
  });

  test('local signing', function(done) {
    $.remote.local_signing = true;

    async.series([
    function(callback) {
      let tx = $.remote.createTransaction('SignerListSet', {
        account: accounts.alice.account,
        signers: [
          {account: accounts.bob.account, weight: 1},
          {account: accounts.carol.account, weight: 2 }
        ],
        signerQuorum: 3
      });

      testutils.submit_transaction(tx, callback);
    },

    function(callback) {
      let tx = $.remote.createTransaction('AccountSet', {account: accounts.alice.account});
      tx.setFee(100);
      tx.setSequence(getAliceSequence());
      tx.setLastLedgerSequenceOffset(5);

      let mTx = Transaction.from_json(tx.getMultiSigningJson());

      [accounts.bob, accounts.carol].forEach(account => {
        let signer = mTx.multiSign( account.account, account.secret);
        assert(signer.Account);
        assert(signer.SigningPubKey);
        assert(signer.TxnSignature);

        tx.addMultiSigner(signer);
      });

      tx.once('submitted', function(res) {
        assert.strictEqual(res.engine_result, 'tesSUCCESS');
        assert.deepEqual(res.tx_json.Signers, tx.getMultiSigners());
        callback();
      });

      tx.submit();
    }
    ], done);
  });

  test('No multi-signers specified for account', function(done) {
    async.series([
    function(callback) {
      let tx = $.remote.createTransaction('AccountSet', {account: accounts.alice.account});
      tx.setFee(100);
      tx.setSequence(getAliceSequence());
      tx.setLastLedgerSequenceOffset(5);

      let mTx = Transaction.from_json(tx.getMultiSigningJson());

      let signer = mTx.multiSign(accounts.bob.account, accounts.bob.secret);
      assert(signer.Account);
      assert(signer.SigningPubKey);
      assert(signer.TxnSignature);

      tx.addMultiSigner(signer);

      tx.once('submitted', function(res) {
        assert.strictEqual(res.engine_result, 'tefNOT_MULTI_SIGNING');
        callback();
      });

      tx.submit();
    }
    ], done);
  });

  test('Attempt to use unspecified signer', function(done) {
    async.series([
    function(callback) {
      let tx = $.remote.createTransaction('SignerListSet', {
        account: accounts.alice.account,
        signers: [
          {account: accounts.bob.account, weight: 1},
        ],
        signerQuorum: 1
      });

      testutils.submit_transaction(tx, callback);
    },

    function(callback) {
      let tx = $.remote.createTransaction('AccountSet', {account: accounts.alice.account});
      tx.setFee(100);
      tx.setSequence(getAliceSequence());
      tx.setLastLedgerSequenceOffset(5);

      let mTx = Transaction.from_json(tx.getMultiSigningJson());

      let signer = mTx.multiSign(accounts.carol.account, accounts.carol.secret);
      assert(signer.Account);
      assert(signer.SigningPubKey);
      assert(signer.TxnSignature);

      tx.addMultiSigner(signer);

      tx.once('submitted', function(res) {
        assert.strictEqual(res.engine_result, 'tefBAD_SIGNATURE');
        callback();
      });

      tx.submit();
    }
    ], done);
  });

  test('Unmet quorum', function(done) {
    async.series([
    function(callback) {
      let tx = $.remote.createTransaction('SignerListSet', {
        account: accounts.alice.account,
        signers: [
          {account: accounts.bob.account, weight: 1},
          {account: accounts.carol.account, weight: 1}
        ],
        signerQuorum: 2
      });

      testutils.submit_transaction(tx, callback);
    },

    function(callback) {
      let tx = $.remote.createTransaction('AccountSet', {account: accounts.alice.account});
      tx.setFee(100);
      tx.setSequence(getAliceSequence());
      tx.setLastLedgerSequenceOffset(5);

      let mTx = Transaction.from_json(tx.getMultiSigningJson());

      let signer = mTx.multiSign(accounts.bob.account, accounts.bob.secret);
      assert(signer.Account);
      assert(signer.SigningPubKey);
      assert(signer.TxnSignature);

      tx.addMultiSigner(signer);

      tx.once('submitted', function(res) {
        assert.strictEqual(res.engine_result, 'tefBAD_QUORUM');
        callback();
      });

      tx.submit();
    }
    ], done);
  });

  test('Unreachable quorum', function(done) {
    async.series([
    function(callback) {
      let tx = $.remote.createTransaction('SignerListSet', {
        account: accounts.alice.account,
        signers: [
          {account: accounts.bob.account, weight: 1},
        ],
        signerQuorum: 2
      });

      tx.once('submitted', function(res) {
        assert.strictEqual(res.engine_result, 'temBAD_QUORUM');
        callback();
      });
      tx.submit();
    },
    ], done);
  });

  test('Invalid signature -- modified tx_json', function(done) {
    async.series([
    function(callback) {
      let tx = $.remote.createTransaction('SignerListSet', {
        account: accounts.alice.account,
        signers: [
          {account: accounts.bob.account, weight: 1},
        ],
        signerQuorum: 1
      });

      testutils.submit_transaction(tx, callback);
    },

    function(callback) {
      let tx = $.remote.createTransaction('AccountSet', {account: accounts.alice.account});
      tx.setFee(100);
      tx.setSequence(getAliceSequence());
      tx.setLastLedgerSequenceOffset(5);

      let mTx = Transaction.from_json(tx.getMultiSigningJson());
      // Tamper with transaction data prior to multi-signing
      mTx.setSequence(getAliceSequence() + 1);

      let signer = mTx.multiSign(accounts.bob.account, accounts.bob.secret);
      assert(signer.Account);
      assert(signer.SigningPubKey);
      assert(signer.TxnSignature);

      tx.addMultiSigner(signer);

      tx.once('submitted', function(res) {
        assert(res.error)
        assert.strictEqual(res.remote.error_message, 'Invalid signature.')
        callback();
      });

      tx.submit();
    }
    ], done);
  });

  test('Invalid signature -- malformed signer', function(done) {
    async.series([
    function(callback) {
      let tx = $.remote.createTransaction('SignerListSet', {
        account: accounts.alice.account,
        signers: [
          {account: accounts.bob.account, weight: 1},
        ],
        signerQuorum: 1
      });

      testutils.submit_transaction(tx, callback);
    },

    function(callback) {
      let tx = $.remote.createTransaction('AccountSet', {account: accounts.alice.account});
      tx.setFee(100);
      tx.setSequence(getAliceSequence());
      tx.setLastLedgerSequenceOffset(5);

      let mTx = Transaction.from_json(tx.getMultiSigningJson());

      let signer = mTx.multiSign(accounts.bob.account, accounts.bob.secret);
      // Tamper with signer after multi-signing
      signer.Account = accounts.carol.account;

      assert(signer.Account);
      assert(signer.SigningPubKey);
      assert(signer.TxnSignature);

      tx.addMultiSigner(signer);

      tx.once('submitted', function(res) {
        assert(res.error)
        assert.strictEqual(res.remote.error_message, 'Invalid signature.')
        callback();
      });

      tx.submit();
    }
    ], done);
  });

  test('Invalid signature -- SigningPubKey non-empty', function(done) {
    async.series([
    function(callback) {
      let tx = $.remote.createTransaction('SignerListSet', {
        account: accounts.alice.account,
        signers: [
          {account: accounts.bob.account, weight: 1},
        ],
        signerQuorum: 1
      });

      testutils.submit_transaction(tx, callback);
    },

    function(callback) {
      let tx = $.remote.createTransaction('AccountSet', {account: accounts.alice.account});
      tx.setFee(100);
      tx.setSequence(getAliceSequence());
      tx.setLastLedgerSequenceOffset(5);

      let mTx = Transaction.from_json(tx.getMultiSigningJson());

      let signer = mTx.multiSign(accounts.bob.account, accounts.bob.secret);
      assert(signer.Account);
      assert(signer.SigningPubKey);
      assert(signer.TxnSignature);

      tx.addMultiSigner(signer);

      tx.once('presubmit', function(res) {
        // SigningPubKey must be empty
        tx.setSigningPubKey(tx.getSigningPubKey());
      });
      tx.once('submitted', function(res) {
        assert(res.error);
        assert.strictEqual(res.remote.error, 'invalidParams');
        callback();
      });

      tx.submit();
    }
    ], done);
  });
});
