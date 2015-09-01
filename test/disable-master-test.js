let assert = require('assert-diff');
let async = require('async');
let Remote = require('ripple-lib').Remote;
let testutils = require('./testutils');

testutils.init_config();

suite('Disabling MasterKey', function() {
  let $ = {};

  setup(function(done) {
    testutils.build_setup().call($, function() {
      $.remote.local_signing = true;
      done();
    });
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test('lsfPasswordSpent', function(done) {
    let password_spent_flag = Remote.flags.account_root.PasswordSpent;
    let regular_key = 'rGLnRYhy5fQK5pxZuMxtsJKrbu5onBpRst';

    async.series(
      [
        function(callback) {
          testutils.create_accounts($.remote, 'root', '1000.0', ['alice'], callback);
        },
        function(callback) {
          $.remote.requestAccountInfo({account: 'alice'}, function(err, info) {
            assert.ifError(err);
            assert(!(info.account_data.Flags & password_spent_flag),
                   'PasswordSpent flag set unexpectedly');
            callback();
          });
        },
        function(callback) {
          let tx = $.remote.createTransaction('SetRegularKey', {
            account: 'alice',
            regular_key: regular_key
          });

          // As a special feature, each account is allowed to perform
          // SetRegularKey transaction without a transaction fee as long as
          // the lsfPasswordSpent flag for the account is not set
          //
          // https://github.com/ripple/rippled/blob/4239880acb5e559446d2067f00dabb31cf102a23/src/ripple/app/transactors/SetRegularKey.cpp#L64-L67
          tx.setFixedFee(0);

          tx.once('submitted', function(res) {
            assert.strictEqual(res.engine_result, 'tesSUCCESS');
          });

          testutils.submit_transaction(tx, callback);
        },
        function(callback) {
          $.remote.requestAccountInfo({account: 'alice'}, function(err, info) {
            assert.ifError(err);
            assert(info.account_data.Flags & password_spent_flag,
                   'PasswordSpent flag not set');
            callback();
          });
        },
        function(callback) {
          // The second SetRegularKey transaction with Fee=0 should fail.
          // The initial engine_result is telINSUF_FEE_P. ripple-lib waits for the
          // transaction's LastLedgerSequence to expire, so the final result is a
          // locally-determined failure: tejMaxLedger
          let tx = $.remote.createTransaction('SetRegularKey', {
            account: 'alice',
            regular_key: regular_key
          });

          tx.setFixedFee(0);
          tx.once('submitted', function(res) {
            assert.strictEqual(res.engine_result, 'telINSUF_FEE_P');
          });

          testutils.submit_transaction(tx, function(err) {
            assert(err);
            assert.strictEqual(err.result, 'tejMaxLedger');
            callback();
          });
        },
        function(callback) {
          let tx = $.remote.createTransaction('Payment', {
            account: 'root',
            destination: 'alice',
            amount: '1'
          });

          testutils.submit_transaction(tx, function(err, res) {
            assert.ifError(err);
            assert.strictEqual(res.engine_result, 'tesSUCCESS');
            callback();
          });
        },
        function(callback) {
          $.remote.requestAccountInfo({account: 'alice'}, function(err, info) {
            assert.ifError(err);
            assert(!(info.account_data.Flags & password_spent_flag),
                   'PasswordSpent flag set unexpectedly');
            callback();
          });
        },
    ], done);
  });

  test('Disable master key', function(done) {
    let account_flags = Remote.flags.account_root;
    let regular_key = 'rGLnRYhy5fQK5pxZuMxtsJKrbu5onBpRst';

    async.series(
      [
        function(callback) {
          testutils.create_accounts($.remote, 'root', '1000.0', ['alice'], callback);
        },
        function(callback) {
          let tx = $.remote.createTransaction('SetRegularKey', {
            account: 'alice',
            regular_key: regular_key
          });

          testutils.submit_transaction(tx, callback);
        },
        function(callback) {
          $.remote.requestAccountInfo({account: 'alice'}, function(err, info) {
            assert.ifError(err);
            assert.strictEqual(info.account_data.RegularKey, regular_key);
            callback();
          });
        },
        function(callback) {
          let tx = $.remote.createTransaction('AccountSet', {
            account: 'alice',
            set_flag: 'asfDisableMaster'
          });

          testutils.submit_transaction(tx, callback);
        },
        function(callback) {
          let tx = $.remote.createTransaction('AccountSet', {
            account: 'alice'
          });

          testutils.submit_transaction(tx, function(err) {
            assert(err);
            assert.strictEqual(err.result, 'tejMaxLedger');
            assert.strictEqual(tx.summary().result.engine_result, 'tefMASTER_DISABLED');
            callback();
          });
        }
    ], done);
  });
});
