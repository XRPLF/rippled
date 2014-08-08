var async       = require('async');
var assert      = require('assert');
var ripple      = require('ripple-lib');
var Amount      = require('ripple-lib').Amount;
var Remote      = require('ripple-lib').Remote;
var Transaction = require('ripple-lib').Transaction;
var Server      = require('./server').Server;
var testutils   = require('./testutils');
var config      = testutils.init_config();

suite('Robust transaction submission', function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, function() {
      $.remote.local_signing = true;

      $.remote.request_subscribe()
      .accounts($.remote.account('root')._account_id)
      .callback(done);
    });
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  // Payment is submitted (without a destination tag)
  // to a destination which requires one.
  //
  // The sequence is now in the future.
  //
  // Immediately subsequent transactions should err
  // with terPRE_SEQ.
  //
  // Gaps in the sequence should be filled with an
  // empty transaction.
  //
  // Transaction should ultimately succeed.
  //
  // Subsequent transactions should be submitted with
  // an up-to-date transction sequence. i.e. the
  // internal sequence should always catch up.

  test('sequence realignment', function(done) {
    var self = this;

    var steps = [

      function createAccounts(callback) {
        self.what = 'Create accounts';
        testutils.create_accounts($.remote, 'root', '20000.0', [ 'alice', 'bob' ], callback);
      },

      function setRequireDestTag(callback) {
        self.what = 'Set RequireDestTag';

        var tx = $.remote.transaction().account_set('alice');
        tx.set_flags('RequireDestTag');

        tx.once('submitted', function(m) {

          assert.strictEqual('tesSUCCESS', m.engine_result);
        });

        tx.once('final', function() {
          callback();
        });

        tx.submit();

        testutils.ledger_wait($.remote, tx);
      },

      function sendInvalidTransaction(callback) {
        self.what = 'Send transaction without a destination tag';

        var tx = $.remote.transaction().payment({
          from: 'root',
          to: 'alice',
          amount: Amount.from_human('1XRP')
        });

        tx.once('submitted', function(m) {
          assert.strictEqual('tefDST_TAG_NEEDED', m.engine_result);
        });

        tx.submit();

        //Invoke callback immediately
        callback();
      },

      function sendValidTransaction(callback) {
        self.what = 'Send normal transaction which should succeed';

        var tx = $.remote.transaction().payment({
          from:    'root',
          to:      'bob',
          amount:  Amount.from_human('1XRP')
        });

        tx.on('submitted', function(m) {
          //console.log('Submitted', m);
        });

        tx.once('resubmitted', function() {
          self.resubmitted = true;
        });

        //First attempt at submission should result in
        //terPRE_SEQ as the sequence is still in the future
        tx.once('submitted', function(m) {
          assert.strictEqual('terPRE_SEQ', m.engine_result);
        });

        tx.once('final', function() {
          callback();
        });

        tx.submit();

        testutils.ledger_wait($.remote, tx);
      },

      function checkPending(callback) {
        self.what = 'Check pending';
        var pending = $.remote.getAccount('root')._transactionManager._pending;
        assert.strictEqual(pending._queue.length, 0, 'Pending transactions persisting');
        callback();
      },

      function verifyBalance(callback) {
        self.what = 'Verify balance';
        testutils.verify_balance($.remote, 'bob', '20001000000', callback);
      }

    ]

    async.series(steps, function(err) {
      assert(!err, self.what + ': ' + err);
      assert(self.resubmitted, 'Transaction failed to resubmit');
      done();
    });
  });

  // Submit a normal payment which should succeed.
  //
  // Remote disconnects immediately after submission
  // and before the validated transaction result is
  // received.
  //
  // Remote reconnects in the future. During this
  // time it is presumed that the transaction should
  // have succeeded, but an immediate response was
  // not possible, as the server was disconnected.
  //
  // Upon reconnection, recent account transaction
  // history is loaded.
  //
  // The submitted transaction should be detected,
  // and the transaction should ultimately succeed.

  test('temporary server disconnection', function(done) {
    var self = this;

    var steps = [

      function createAccounts(callback) {
        self.what = 'Create accounts';
        testutils.create_accounts($.remote, 'root', '20000.0', [ 'alice' ], callback);
      },

      function submitTransaction(callback) {
        self.what = 'Submit a transaction';

        var tx = $.remote.transaction().payment({
          from: 'root',
          to: 'alice',
          amount: Amount.from_human('1XRP')
        });

        tx.submit();

        setImmediate(function() {
          $.remote.once('disconnect', function remoteDisconnected() {
            assert(!$.remote._connected);

            tx.once('final', function(m) {
              assert.strictEqual(m.engine_result, 'tesSUCCESS');
              callback();
            });

            $.remote.connect(function() {
              testutils.ledger_wait($.remote, tx);
            });
          });

          $.remote.disconnect();
        });
      },

      function waitLedger(callback) {
        self.what = 'Wait ledger';
        $.remote.once('ledger_closed', function() {
          callback();
        });
        $.remote.ledger_accept();
      },

      function checkPending(callback) {
        self.what = 'Check pending';
        var pending = $.remote.getAccount('root')._transactionManager._pending;
        assert.strictEqual(pending._queue.length, 0, 'Pending transactions persisting');
        callback();
      },

      function verifyBalance(callback) {
        self.what = 'Verify balance';
        testutils.verify_balance($.remote, 'alice', '20001000000', callback);
      }

    ]

    async.series(steps, function(err) {
      assert(!err, self.what + ': ' + err);
      done();
    });
  });

  test('temporary server disconnection -- reconnect after max ledger wait', function(done) {
    var self = this;

    var steps = [

      function createAccounts(callback) {
        self.what = 'Create accounts';
        testutils.create_accounts($.remote, 'root', '20000.0', [ 'alice' ], callback);
      },

      function waitLedgers(callback) {
        self.what = 'Wait ledger';
        $.remote.once('ledger_closed', function() {
          callback();
        });
        $.remote.ledger_accept();
      },

      function verifyBalance(callback) {
        self.what = 'Verify balance';
        testutils.verify_balance($.remote, 'alice', '20000000000', callback);
      },

      function submitTransaction(callback) {
        self.what = 'Submit a transaction';

        var tx = $.remote.transaction().payment({
          from: 'root',
          to: 'alice',
          amount: Amount.from_human('1XRP')
        });

        tx.once('submitted', function(m) {
          assert.strictEqual(m.engine_result, 'tesSUCCESS');

          var handleMessage = $.remote._handleMessage;
          $.remote._handleMessage = function(){};

          var ledgers = 0;

          ;(function nextLedger() {
            if (++ledgers > 8) {
              tx.once('final', function() { callback(); });
              $.remote._handleMessage = handleMessage;
              $.remote.disconnect(function() {
                assert(!$.remote._connected);
                var pending = $.remote.getAccount('root')._transactionManager._pending;
                assert.strictEqual(pending._queue.length, 1, 'Pending transactions persisting');
                $.remote.connect();
              });
            } else {
              $.remote._getServer().once('ledger_closed', function() {
                setTimeout(nextLedger, 20);
              });
              $.remote.ledger_accept();
            }
          })();
        });

        tx.submit();
      },

      function waitLedgers(callback) {
        self.what = 'Wait ledgers';

        var ledgers = 0;

        ;(function nextLedger() {
          $.remote.once('ledger_closed', function() {
            if (++ledgers === 3) {
              callback();
            } else {
              setTimeout(nextLedger, process.env.TRAVIS ? 400 : 100 );
            }
          });
          $.remote.ledger_accept();
        })();
      },

      function checkPending(callback) {
        self.what = 'Check pending';
        var pending = $.remote.getAccount('root')._transactionManager._pending;
        assert.strictEqual(pending._queue.length, 0, 'Pending transactions persisting');
        callback();
      },

      function verifyBalance(callback) {
        self.what = 'Verify balance';
        testutils.verify_balance($.remote, 'alice', '20001000000', callback);
      }

    ]

    async.series(steps, function(err) {
      assert(!err, self.what + ': ' + err);
      done();
    });
  });

  // Submit request times out
  //
  // Since the transaction ID is generated locally, the
  // transaction should still validate from the account
  // transaction stream, even without a response to the
  // original submit request

  test('submission timeout', function(done) {
    var self = this;

    var steps = [

      function createAccounts(callback) {
        self.what = 'Create accounts';
        testutils.create_accounts($.remote, 'root', '20000.0', [ 'alice' ], callback);
      },

      function submitTransaction(callback) {
        self.what = 'Submit a transaction whose submit request times out';

        var tx = $.remote.transaction().payment({
          from: 'root',
          to: 'alice',
          amount: Amount.from_human('1XRP')
        });

        var timed_out = false;

        $.remote.getAccount('root')._transactionManager._submissionTimeout = 0;

        // A response from transaction submission should never
        // actually be received
        tx.once('timeout', function() { timed_out = true; });

        tx.once('final', function(m) {
          assert(timed_out, 'Transaction submission failed to time out');
          assert.strictEqual(m.engine_result, 'tesSUCCESS');
          callback();
        });

        tx.submit();

        testutils.ledger_wait($.remote, tx);
      },

      function checkPending(callback) {
        self.what = 'Check pending';
        assert.strictEqual($.remote.getAccount('root')._transactionManager._pending.length(), 0, 'Pending transactions persisting');
        callback();
      },

      function verifyBalance(callback) {
        self.what = 'Verify balance';
        testutils.verify_balance($.remote, 'alice', '20001000000', callback);
      }

    ]

    async.series(steps, function(err) {
      assert(!err, self.what + ': ' + err);
      done();
    });
  });

  // Subscribing to accounts_proposed will result in ripple-lib
  // being streamed non-validated (proposed) transactions
  //
  // This test ensures that only validated transactions will
  // trigger a transaction success event

  test('subscribe to accounts_proposed', function(done) {
    var self = this;

    var series = [

      function subscribeToAccountsProposed(callback) {
        self.what = 'Subscribe to accounts_proposed';

        $.remote.requestSubscribe()
        .addAccountProposed('root')
        .callback(callback);
      },

      function submitTransaction(callback) {
        self.what = 'Submit a transaction';

        var tx = $.remote.transaction().accountSet('root');

        var receivedProposedTransaction = false;

        $.remote.on('transaction', function(tx) {
          if (tx.status === 'proposed') {
            receivedProposedTransaction = true;
          }
        });

        tx.submit(function(err, m) {
          assert(!err, err);
          assert(receivedProposedTransaction, 'Did not received proposed transaction from stream');
          assert(m.engine_result, 'tesSUCCESS');
          assert(m.validated, 'Transaction is finalized with invalidated transaction stream response');
          done();
        });

        testutils.ledger_wait($.remote, tx);
      }

    ]

    async.series(series, function(err, m) {
      assert(!err, self.what + ': ' + err);
      done();
    });
  });

  // Validate that LastLedgerSequence works
  test('set LastLedgerSequence', function(done) {
    var self = this;

    var series = [

      function createAccounts(callback) {
        self.what = 'Create accounts';
        testutils.create_accounts($.remote, 'root', '20000.0', [ 'alice' ], callback);
      },

      function submitTransaction(callback) {
        var tx = $.remote.transaction().payment('root', 'alice', '1');
        tx.lastLedger(0);

        tx.once('submitted', function(m) {
          assert.strictEqual(m.engine_result, 'tefMAX_LEDGER');
          callback();
        });

        tx.submit();
      }

    ]

    async.series(series, function(err) {
      assert(!err, self.what);
      done();
    });
  });
});
