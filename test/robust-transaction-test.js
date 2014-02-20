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

    self.sent_transactions = [ ];

    var steps = [

      function (callback) {
        self.what = 'Create accounts';
        testutils.create_accounts($.remote, 'root', '20000.0', [ 'alice', 'bob' ], callback);
      },

      function (callback) {
        $.remote.ledger_accept(callback);
      },

      function (callback) {
        self.what = 'Set RequireDestTag';

        var tx = $.remote.transaction().account_set('alice');
        tx.set_flags('RequireDestTag');

        tx.once('submitted', function(m) {
          assert.strictEqual('tesSUCCESS', m.engine_result);
          $.remote.ledger_accept();
        });

        tx.submit(callback);
      },

      function (callback) {
        $.remote.getAccount('root').on('transaction-outbound', function(tx) {
          if (tx.transaction.TransactionType === 'Payment' && tx.transaction.Amount === '1000000') {
            self.sent_transactions.push(tx.transaction);
          }
        });
        setTimeout(callback, 100);
      },

      function (callback) {
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

      function (callback) {
        self.what = 'Send normal transaction which should succeed';

        var tx = $.remote.transaction().payment({
          from: 'root',
          to: 'bob',
          amount: Amount.from_human('1XRP')
        });

        //First attempt at submission should result in
        //terPRE_SEQ as the sequence is still in the future
        tx.once('submitted', function(m) {
          $.remote.ledger_accept();
          assert.strictEqual('terPRE_SEQ', m.engine_result);
        });

        tx.submit(callback);
      },

      function (callback) {
        self.what = 'Send another normal transaction which should succeed';

        var tx = $.remote.transaction().payment({
          from: 'root',
          to: 'bob',
          amount: Amount.from_human('1XRP')
        });

        tx.once('submitted', function(m) {
          $.remote.ledger_accept();
          assert.strictEqual(m.engine_result, 'tesSUCCESS');
        });

        tx.submit(callback);
      },

      function checkPending(callback) {
        assert.strictEqual($.remote.getAccount('root')._transactionManager._pending.length(), 0, 'Pending transactions persisting');
        callback();
      },

      function (callback) {
        testutils.verify_balance($.remote, 'bob', '20002000000', callback);
      }
    ]

    async.series(steps, function(err) {
      assert(!err, self.what + ': ' + err);

      assert(self.resubmitted, 'Transaction failed to resubmit');

      assert(self.sent_transactions.length, 'No payments sent');

      assert.strictEqual(self.sent_transactions.length, 2, 'Payment submitted more than once');

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

    self.sent_transactions = [ ];

    var steps = [

      function (callback) {
        self.what = 'Create accounts';
        testutils.create_accounts($.remote, 'root', '20000.0', [ 'alice' ], callback);
      },

      function (callback) {
        $.remote.getAccount('root').on('transaction-outbound', function(tx) {
          if (tx.transaction.TransactionType === 'Payment' && tx.transaction.Amount === '1000000') {
            self.sent_transactions.push(tx.transaction);
          }
        });
        setTimeout(callback, 100);
      },

      function (callback) {
        self.what = 'Submit a transaction';

        var tx = $.remote.transaction().payment({
          from: 'root',
          to: 'alice',
          amount: Amount.from_human('1XRP')
        });

        tx.on('submitted', function() {
          $.remote.ledger_accept();
        });

        tx.submit();

        process.nextTick(function() {
          $.remote.disconnect();

          setTimeout(function() {
            assert(!$.remote._connected);

            tx.once('error', callback);
            tx.once('success', function() { callback(); });

            $.remote.connect();
          }, 450);
        });
      },

      function checkPending(callback) {
        assert.strictEqual($.remote.getAccount('root')._transactionManager._pending.length(), 0, 'Pending transactions persisting');
        callback();
      },

      function (callback) {
        setTimeout(callback, 1000 * 2);
      },

      function (callback) {
        testutils.verify_balance($.remote, 'alice', '20001000000', callback);
      }

    ]

    async.series(steps, function(err) {
      assert(!err, self.what + ': ' + err);

      assert(self.sent_transactions.length, 'Payment did not send');

      assert.strictEqual(self.sent_transactions.length, 1, 'Payment sent more than once');

      done();
    });
  });

  test('temporary server disconnection -- reconnect after max ledger wait', function(done) {
    var self = this;

    self.sent_transactions = [ ];

    var steps = [
      function (callback) {
        self.what = 'Create accounts';
        testutils.create_accounts($.remote, 'root', '20000.0', [ 'alice' ], callback);
      },

      function (callback) {
        $.remote.getAccount('root').on('transaction-outbound', function(tx) {
          if (tx.transaction.TransactionType === 'Payment' && tx.transaction.Amount === '1000000') {
            self.sent_transactions.push(tx.transaction);
          }
        });

        callback();
      },

      function (callback) {
        self.what = 'Submit a transaction';

        var tx = $.remote.transaction().payment({
          from: 'root',
          to: 'alice',
          amount: Amount.from_human('1XRP')
        });

        tx.once('submitted', function(m) {
          assert.strictEqual(m.engine_result, 'tesSUCCESS');

          $.remote.ledger_accept();

          process.nextTick(function() {
            $.remote.disconnect();
          });

          setTimeout(function() {
            assert(!$.remote._connected);

            tx.once('error', callback);
            tx.once('success', function() { callback(); });

            $.remote.connect();
          }, 2000);
        });

        tx.submit();
      },

      function checkPending(callback) {
        assert.strictEqual($.remote.getAccount('root')._transactionManager._pending.length(), 0, 'Pending transactions persisting');
        callback();
      },

      function (callback) {
        testutils.verify_balance($.remote, 'alice', '20001000000', callback);
      }
    ]

    async.series(steps, function(err) {
      assert(!err, self.what + ': ' + err);

      assert(self.sent_transactions.length, 'Payment did not send');

      assert.strictEqual(self.sent_transactions.length, 1, 'Payment sent more than once');

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

    $.remote.local_signing = true;

    self.sent_transactions = [ ];

    var steps = [
      function (callback) {
        self.what = 'Create accounts';
        testutils.create_accounts($.remote, 'root', '20000.0', [ 'alice' ], callback);
      },

      function (callback) {
        $.remote.getAccount('root').on('transaction-outbound', function(tx) {
          if (tx.transaction.TransactionType === 'Payment' && tx.transaction.Amount === '1000000') {
            self.sent_transactions.push(tx.transaction);
          }
        });
        setTimeout(callback, 100);
      },

      function (callback) {
        self.what = 'Submit a transaction whose submit request times out';

        var tx = $.remote.transaction().payment({
          from: 'root',
          to: 'alice',
          amount: Amount.from_human('1XRP')
        });

        $.remote.getAccount('root')._transactionManager._submissionTimeout = 0.00001;

        // A response from transaction submission should never
        // actually be received
        tx.once('timeout', function() { self.timed_out = true; });

        tx.submit(callback);
      },

      function (callback) {
        setTimeout(callback, 1000 * 2);
      },

      function checkPending(callback) {
        assert.strictEqual($.remote.getAccount('root')._transactionManager._pending.length(), 0, 'Pending transactions persisting');
        callback();
      },

      function (callback) {
        testutils.verify_balance($.remote, 'alice', '20001000000', callback);
      }
    ]

    async.series(steps, function(err) {
      assert(!err, self.what + ': ' + err);

      assert.strictEqual(self.sent_transactions.length, 1, 'Payment sent more than once');

      //assert(self.timed_out, 'Transaction submission failed to time out');

      assert(self.sent_transactions.length, 'Payment was not sent');

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
      function (callback) {
        self.what = 'Subscribe to accounts_proposed';

        $.remote.requestSubscribe()
        .addAccountProposed('root')
        .callback(callback);
      },
      function (callback) {
        self.what = 'Create accounts';

        var payment = $.remote.transaction().payment({
          from: 'root',
          to: 'alice',
          amount: Amount.from_human('20000XRP')
        });

        payment.submit(function(err, m) {
          assert.ifError(err);
          assert(m.engine_result, 'tesSUCCESS');
          assert(m.validated, 'Transaction is finalized with invalidated transaction stream response');
          done();
        });
      }
    ]

    async.series(series, function(err, m) {
      assert(!err, self.what + ': ' + err);
      done();
    });
  });
});
