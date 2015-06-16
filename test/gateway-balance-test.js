/**
   Test the "gateway_balances" RPC command.
*/

var assert       = require('assert');
var async        = require('async');
var testutils    = require('./testutils');
var config       = testutils.init_config();

var Account      = require('ripple-lib').UInt160;
var LedgerState  = require('./ledger-state').LedgerState;
var Request      = require('ripple-lib').Request;

function noop() {}

function account_objects(remote, account, params, callback) {
  if (lodash.isFunction(params)) {
    callback = params;
    params = null;
  }

  var request = new Request(remote, 'account_objects');
  request.message.account = Account.json_rewrite(account);
  lodash.forOwn(params || {}, function(v, k) { request.message[k] = v; });

  request.callback(callback);
}

function filter_threading_fields(entries) {
  return entries.map(function(entry) {
    return lodash.omit(entry, ['PreviousTxnID', 'PreviousTxnLgrSeq']);
  });
}

suite('Gateway balance', function() {
  var $ = {};
  var opts = {};

  // Taken from account_objects-test.js.
  // TODO: Figure out how to share this code with that file.

  // A declarative description of the ledger
  var ledger_state = {
    accounts: {
      // Gateways
      G1 : {balance: ["1000.0"]},
      G2 : {balance: ["1000.0"]},

      // Bob has two RippleState and two Offer account objects.
      bob : {
        balance: ["1000.0", "1000/USD/G1",
                            "1000/USD/G2"],
        // these offers will be in `Sequence`
        offers: [["100.0", "1/USD/bob"],
                 ["100.0", "1/USD/G1"]]
      }
    }
  };

  // After setup we bind the remote to `account_objects` helper above.
  var request_account_objects;
  var bob;
  var G1;
  var G2;

  suiteSetup(function(done) {
    testutils.build_setup().call($, function() {
      request_account_objects = account_objects.bind(null, $.remote);
      var ledger = new LedgerState(ledger_state,
                                   assert, $.remote,
                                   config);

      // Get references to the account objects for usage later.
      bob = Account.json_rewrite('bob');
      G1 = Account.json_rewrite('G1');
      G2 = Account.json_rewrite('G2');

      // Run the ledger setup util. This compiles the declarative description
      // into a series of transactions and executes them.
      ledger.setup(noop /*logger*/, function(){
        done();
      })
    });
  });

  suiteTeardown(function(done) {
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
        assert(result.account == Account.json_rewrite('mtgox'));
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
