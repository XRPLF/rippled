/**
   Test the "gateway_balances" RPC command.
*/

var async        = require('async');
var testutils    = require('./testutils');
var config       = testutils.init_config();

var Account      = require('ripple-lib').UInt160;
var Request      = require('ripple-lib').Request;
var assert       = require('assert');

suite('Gateway balance', function() {
  var $ = {};
  var opts = {};

  setup(function(done) {
    testutils.build_setup(opts).call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test('Gateway balance', function (done) {
    var self = this;

    var steps = [
      function(callback) {
        testutils.create_accounts(
          $.remote,
          'root',
          '20000.0',
          ['alice', 'bob', 'mtgox'],
          callback
        );
      },

      function(callback) {
        var tx = $.remote.createTransaction('TrustSet', {
          account: 'mtgox',
          limit: '100/USD/alice'
        });
        testutils.submit_transaction(tx, callback);
      },

      function(state, callback) {
        var tx = $.remote.createTransaction('TrustSet', {
          account: 'mtgox',
          limit: '100/USD/bob'
        });
        testutils.submit_transaction(tx, callback);
      },

      function(state, callback) {
        $.remote.once('ledger_closed', function() { callback(); });
        $.remote.ledger_accept();
      },

      function(callback) {
        testutils.verify_balance(
          $.remote,
          [ 'alice', 'bob', 'mtgox'],
          '19999999988',
          callback
        );
      },

      function(callback) {
        var request = new Request($.remote, 'gateway_balances');
        request.message.account = Account.json_rewrite('mtgox');
        request.message.ledger = 'validated';
        request.message.hotwallet = [
          Account.json_rewrite('alice'),
          Account.json_rewrite('bob')
        ];
        request.callback(callback);
      },

      function(result, callback) {
        assert(callback.account == Account.json_rewrite('mtgox'));
        // TODO: where are the rest of the fields?!
        callback();
      },
    ];

    async.waterfall(steps, function (error) {
      assert(!error, self.what + ': ' + error);
      done();
    });
  });
});
