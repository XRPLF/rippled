var async       = require('async');
var assert      = require('assert');
var Amount      = require('ripple-lib').Amount;
var Remote      = require('ripple-lib').Remote;
var Transaction = require('ripple-lib').Transaction;
var Server      = require('./server').Server;
var testutils   = require('./testutils');
var config      = testutils.init_config();

suite('TrustSet with NoRipple flag', function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test('no-ripple', function(done) {
    var self = this;

    var steps = [

      function (callback) {
        self.what = 'Create accounts.';
        testutils.create_accounts($.remote, 'root', '10000.0', [ 'alice' ], callback);
      },

      function (callback) {
        self.what = 'Check a non-existent credit limit';

        $.remote.request_ripple_balance('alice', 'root', 'USD', 'CURRENT', function(err) {
          assert.strictEqual('remoteError', err.error);
          assert.strictEqual('entryNotFound', err.remote.error);
          callback();
        });
      },

      function (callback) {
        self.what = 'Create a credit limit with NoRipple flag';

        var tx = $.remote.transaction();
        tx.ripple_line_set('root', '100/USD/alice');
        tx.set_flags('NoRipple');

        tx.once('error', callback);
        tx.once('proposed', function(res) {
          $.remote.ledger_accept();
          callback();
        });

        tx.submit();
      },

      function (callback) {
        self.what = 'Check no-ripple sender';

        $.remote.request_account_lines('root', void(0), 'CURRENT', function(err, m) {
          if (err) return callback(err);
          assert(typeof m === 'object');
          assert(Array.isArray(m.lines));
          assert(m.lines[0].no_ripple);
          callback();
        });
      },

      function (callback) {
        self.what = 'Check no-ripple destination';

        $.remote.request_account_lines('alice', void(0), 'CURRENT', function(err, m) {
          if (err) return callback(err);
          assert(typeof m === 'object');
          assert(Array.isArray(m.lines));
          assert(m.lines[0].no_ripple_peer);
          callback();
        });
      },

      function (callback) {
        self.what = 'Create a credit limit with ClearNoRipple flag';

        var tx = $.remote.transaction();
        tx.ripple_line_set('root', '100/USD/alice');
        tx.set_flags('ClearNoRipple');

        tx.once('error', callback);
        tx.once('proposed', function(res) {
          $.remote.ledger_accept();
          callback();
        });

        tx.submit();
      },

      function (callback) {
        self.what = 'Check no-ripple cleared sender';

        $.remote.request_account_lines('root', void(0), 'CURRENT', function(err, m) {
          if (err) return callback(err);
          assert(typeof m === 'object');
          assert(Array.isArray(m.lines));
          assert(!m.lines[0].no_ripple);
          callback();
        });
      },

      function (callback) {
        self.what = 'Check no-ripple cleared destination';

        $.remote.request_account_lines('alice', void(0), 'CURRENT', function(err, m) {
          if (err) return callback(err);
          assert(typeof m === 'object');
          assert(Array.isArray(m.lines));
          assert(!m.lines[0].no_ripple_peer);
          callback();
        });
      },
    ]

    async.series(steps, function(err) {
      assert(!err, self.what);
      done();
    });
  });
});

