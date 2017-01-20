var async       = require('async');
var assert      = require('assert');
var ripple      = require('ripple-lib');
var Amount      = require('ripple-lib').Amount;
var Remote      = require('ripple-lib').Remote;
var Transaction = require('ripple-lib').Transaction;
var Server      = require('./server').Server;
var testutils   = require('./testutils');
var config      = testutils.init_config();

suite('NoRipple', function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test('set and clear NoRipple', function(done) {
    var self = this;

    var steps = [

      function (callback) {
        self.what = 'Create accounts.';
        testutils.create_accounts($.remote, 'root', '10000.0', [ 'alice' ], callback);
      },

      function (callback) {
        self.what = 'Check a non-existent credit limit';

        $.remote.request_ripple_balance('alice', 'root', 'USD', 'current', function(err) {
          assert.strictEqual('remoteError', err.error);
          assert.strictEqual('entryNotFound', err.remote.error);
          callback();
        });
      },

      function (callback) {
        self.what = 'Create a credit limit with NoRipple flag';

        var tx = $.remote.transaction();
        tx.trustSet('root', '100/USD/alice');
        tx.setFlags('NoRipple');

        tx.once('submitted', function() {
          $.remote.ledger_accept();
        });

        tx.once('error', callback);
        tx.once('proposed', function(res) {
          callback();
        });

        tx.submit();
      },

      function (callback) {
        self.what = 'Check no-ripple sender';

        $.remote.requestAccountLines({ account: 'root', ledger: 'validated' }, function(err, m) {
          if (err) return callback(err);
          assert(typeof m === 'object');
          assert(Array.isArray(m.lines));
          assert(m.lines[0].no_ripple);
          callback();
        });
      },

      function (callback) {
        self.what = 'Check no-ripple destination';

        $.remote.requestAccountLines({ account: 'alice', ledger: 'validated' }, function(err, m) {
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
        tx.trustSet('root', '100/USD/alice');
        tx.setFlags('ClearNoRipple');

        tx.once('submitted', function() {
          $.remote.ledger_accept();
        });

        tx.once('error', callback);
        tx.once('proposed', function(res) {
          callback();
        });

        tx.submit();
      },

      function (callback) {
        self.what = 'Check no-ripple cleared sender';

        $.remote.requestAccountLines({ account: 'root', ledger: 'validated' }, function(err, m) {
          if (err) return callback(err);
          assert(typeof m === 'object');
          assert(Array.isArray(m.lines));
          assert(!m.lines[0].no_ripple);
          callback();
        });
      },

      function (callback) {
        self.what = 'Check no-ripple cleared destination';

        $.remote.requestAccountLines({ account: 'alice', ledger: 'validated' }, function(err, m) {
          if (err) return callback(err);
          assert(typeof m === 'object');
          assert(Array.isArray(m.lines));
          assert(!m.lines[0].no_ripple_peer);
          callback();
        });
      }

    ]

    async.series(steps, function(err) {
      assert(!err, self.what + ': ' + err);
      done();
    });
  });

  test('set NoRipple on line with negative balance', function(done) {
    // Setting NoRipple on a line with negative balance should fail
    var self = this;

    var steps = [

      function (callback) {
        self.what = 'Create accounts';

        testutils.create_accounts(
          $.remote,
          'root',
          '10000.0',
          [ 'alice', 'bob', 'carol' ],
          callback);
      },

      function (callback) {
        self.what = 'Set credit limits';

        testutils.credit_limits($.remote, {
          bob: '100/USD/alice',
          carol: '100/USD/bob'
        }, callback);
      },

      function (callback) {
        self.what = 'Payment';

        var tx = $.remote.transaction();
        tx.buildPath(true);
        tx.payment('alice', 'carol', '50/USD/carol');

        tx.once('submitted', function(m) {
          assert.strictEqual(m.engine_result, 'tesSUCCESS');
          $.remote.ledger_accept();
        });

        tx.submit(callback);
      },

      function (callback) {
        self.what = 'Set NoRipple alice';

        var tx = $.remote.transaction();
        tx.trustSet('alice', '100/USD/bob');
        tx.setFlags('NoRipple');

        tx.once('submitted', function(m) {
          assert.strictEqual(m.engine_result, 'tesSUCCESS');
          $.remote.ledger_accept();
        });

        tx.submit(callback);
      },

      function (callback) {
        self.what = 'Set NoRipple carol';

        var tx = $.remote.transaction();
        tx.trustSet('bob', '100/USD/carol');
        tx.setFlags('NoRipple');

        tx.once('submitted', function(m) {
          assert.strictEqual(m.engine_result, 'tesSUCCESS');
          $.remote.ledger_accept();
        });

        tx.submit(callback);
      },

      function (callback) {
        self.what = 'Find path alice > carol';

        var request = $.remote.requestRipplePathFind('alice', 'carol', '1/USD/carol', [ { currency: 'USD' } ]);
        request.callback(function(err, paths) {
          assert.ifError(err);
          assert(Array.isArray(paths.alternatives));
          assert.strictEqual(paths.alternatives.length, 1);
          callback();
        });
      },

      function (callback) {
        $.remote.requestAccountLines({ account: 'alice' }, function(err, res) {
          assert.ifError(err);
          assert.strictEqual(typeof res, 'object');
          assert(Array.isArray(res.lines));
          assert.strictEqual(res.lines.length, 1);
          assert(!(res.lines[0].no_ripple));
          callback();
        });
      }

    ]

    async.series(steps, function(error) {
      assert(!error, self.what + ': ' + error);
      done();
    });
  });

  test('pairwise NoRipple', function(done) {
    var self = this;

    var steps = [

      function (callback) {
        self.what = 'Create accounts';

        testutils.create_accounts(
          $.remote,
          'root',
          '10000.0',
          [ 'alice', 'bob', 'carol' ],
          callback);
      },

      function (callback) {
        self.what = 'Set credit limits';

        testutils.credit_limits($.remote, {
          bob: '100/USD/alice',
          carol: '100/USD/bob'
        }, callback);
      },

      function (callback) {
        self.what = 'Set NoRipple alice';

        var tx = $.remote.transaction();
        tx.trustSet('bob', '100/USD/alice');
        tx.setFlags('NoRipple');

        tx.once('submitted', function(m) {
          assert.strictEqual(m.engine_result, 'tesSUCCESS');
          $.remote.ledger_accept();
        });

        tx.submit(callback);
      },

      function (callback) {
        self.what = 'Set NoRipple carol';

        var tx = $.remote.transaction();
        tx.trustSet('bob', '100/USD/carol');
        tx.setFlags('NoRipple');

        tx.once('submitted', function(m) {
          assert.strictEqual(m.engine_result, 'tesSUCCESS');
          $.remote.ledger_accept();
        });

        tx.submit(callback);
      },

      function (callback) {
        self.what = 'Find path alice > carol';

        var request = $.remote.requestRipplePathFind('alice', 'carol', '1/USD/carol', [ { currency: 'USD' } ]);
        request.callback(function(err, paths) {
          assert.ifError(err);
          assert(Array.isArray(paths.alternatives));
          assert.strictEqual(paths.alternatives.length, 0);
          callback();
        });
      },

      function (callback) {
        self.what = 'Payment';

        var tx = $.remote.transaction();
        tx.buildPath(true);
        tx.payment('alice', 'carol', '1/USD/carol');

        tx.once('submitted', function(m) {
          assert.strictEqual(m.engine_result, 'tecPATH_DRY');
          $.remote.ledger_accept();
          callback();
        });

        tx.submit();
      }

    ]

    async.series(steps, function(error) {
      assert(!error, self.what + ': ' + error);
      done();
    });
  });
});

suite('Default ripple', function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test('Set default ripple on account, check new trustline', function(done) {
    var steps = [
      function (callback) {
        testutils.create_accounts(
          $.remote,
          'root',
          '10000.0',
          [ 'alice', 'bob' ],
          { default_rippling: false },
          callback);
      },
      function (callback) {
        var tx = $.remote.createTransaction('AccountSet', {
          account: 'bob',
          set_flag: 8
        });
        testutils.submit_transaction(tx, callback);
      },
      function (callback) {
        var tx = $.remote.createTransaction('TrustSet', {
          account: 'root',
          limit: '100/USD/alice'
        });
        testutils.submit_transaction(tx, callback);
      },
      function (callback) {
        var tx = $.remote.createTransaction('TrustSet', {
          account: 'root',
          limit: '100/USD/bob'
        });
        testutils.submit_transaction(tx, callback);
      },
      function (callback) {
        $.remote.requestAccountLines({ account: 'root', peer: 'alice' }, function(err, m) {
          assert.ifError(err);
          assert(Array.isArray(m.lines));
          assert(m.lines[0].no_ripple_peer,
            'Trustline should have no_ripple_peer set');
          callback();
        });
      },
      function (callback) {
        $.remote.requestAccountLines({ account: 'alice', peer: 'root' }, function(err, m) {
          assert.ifError(err);
          assert(Array.isArray(m.lines));
          assert(m.lines[0].no_ripple,
            'Trustline should have no_ripple set');
          callback();
        });
      },
      function (callback) {
        $.remote.requestAccountLines({ account: 'root', peer: 'bob' }, function(err, m) {
          assert.ifError(err);
          assert(Array.isArray(m.lines));
          assert(!m.lines[0].no_ripple,
            'Trustline should not have no_ripple set');
          callback();
        });
      },
      function (callback) {
        $.remote.requestAccountLines({ account: 'bob', peer: 'root' }, function(err, m) {
          assert.ifError(err);
          assert(Array.isArray(m.lines));
          assert(!m.lines[0].no_ripple_peer,
            'Trustline should not have no_ripple_peer set');
          callback();
        });
      }
    ]

    async.series(steps, function(error) {
      assert(!error, error);
      done();
    });
  });

});
