var async       = require('async');
var assert      = require('assert');
var Amount      = require('ripple-lib').Amount;
var Remote      = require('ripple-lib').Remote;
var Transaction = require('ripple-lib').Transaction;
var Server      = require('./server').Server;
var testutils   = require('./testutils');
var config      = testutils.init_config();

var accounts = [
  'alice', 'bob', 'carol',        // end users
  'amazon', 'bitstamp', 'mtgox']; // Gateways.
var funding = '1000000000.0';

// Demonstrates RIPD-639: https://ripplelabs.atlassian.net/browse/RIPD-639
var RIPD_639_FIXED = false;

suite('Offer tests through two orderbooks', function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test('ripple payment between two order books', function (done) {
    var self = this;

    var steps = [
      function (callback) {
          self.what = 'Create accounts';

          testutils.create_accounts(
            $.remote,
            'root',
            funding,
            accounts,
            callback
          );
      },

      function (callback) {
        self.what = 'Wait ledger';

        $.remote.once('ledger_closed', function() {
          callback();
        });

        $.remote.ledger_accept();
      },

      function(callback) {
        self.what = 'Verify balance';

        testutils.verify_balance(
            $.remote,
            accounts,
            funding,
            callback);
      },

      function (callback) {
        self.what = 'Set credit limits 1';

        testutils.credit_limits($.remote, {
          'alice' : '1/USD/bitstamp',
          'bob' : '1/USD/amazon',
          'carol' : '1/USD/amazon',
        },
        callback);
      },

      function (callback) {
        self.what = 'Set credit limits 2';

        testutils.credit_limits($.remote, {
          'carol' : '1/USD/mtgox',
        },
        callback);
      },

      function (callback) {
        self.what = 'Carol gets USD/mtgox.';
        testutils.payment($.remote, 'mtgox', 'carol', '1/USD/mtgox', callback);
      },
      function (callback) {
        self.what = 'Carol gets USD/amazon.';
        testutils.payment($.remote, 'amazon', 'carol', '1/USD/amazon', callback);
      },
      function (callback) {
        self.what = 'Alice gets USD/bitstamp.';
        testutils.payment($.remote, 'bitstamp', 'alice', '1/USD/bitstamp', callback);
      },

      function (callback) {
        self.what = 'Create offer Carol 1';
        var tx = $.remote.transaction('OfferCreate', {
          account: 'carol',
          taker_pays: '1/USD/bitstamp',
          taker_gets: '1/USD/mtgox'
        });

        tx.submit(function(err, m) {
          assert.ifError(err);
          assert.strictEqual(m.engine_result, 'tesSUCCESS');
          callback();
        });

        testutils.ledger_wait($.remote, tx);
      },

      function (callback) {
        self.what = 'Create offer Carol 2';
        var tx = $.remote.transaction('OfferCreate', {
          account: 'carol',
          taker_pays: '1/USD/mtgox',
          taker_gets: '1/USD/amazon'
        });

        tx.submit(function(err, m) {
          assert.ifError(err);
          assert.strictEqual(m.engine_result, 'tesSUCCESS');
          callback();
        });

        testutils.ledger_wait($.remote, tx);
      },

      function (callback) {
        self.what = "Find path from alice to bob";
        $.remote.request_ripple_path_find("alice", "bob", "1/USD/bob",
          [ { 'currency' : "USD" } ])
          .on('success', function (m) {
              if (RIPD_639_FIXED) {
                  assert.strictEqual(1, m.alternatives.length)
                  var path = m.alternatives[0];
                  assert.strictEqual(2, path.paths_canonical.length);
              } else {
                  assert.strictEqual(0, m.alternatives.length)
              }
              callback();
          })
        .request();
      },
    ];

    async.waterfall(steps, function(err) {
      assert(!err, self.what + ': ' + err);
      done();
    });
  });
});
