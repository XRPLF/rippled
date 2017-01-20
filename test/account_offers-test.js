/* -------------------------------- REQUIRES -------------------------------- */

var assert       = require('assert-diff');
var lodash       = require('lodash');

var testutils    = require('./testutils');
var LedgerState  = require('./ledger-state').LedgerState;

var config       = testutils.init_config();
// We just use equal instead of strictEqual everywhere.
assert.options.strict = true;

/* ---------------------------------- TEST ---------------------------------- */

function makeSuite(name, ledger_state, tests) {
  suite(name, function() {
    // build_(setup|teardown) utils functions set state on this context var.
    var context = {};

    // This runs only once
    suiteSetup(function(done) {
      testutils.build_setup().call(context, function() {
        var ledger = new LedgerState(ledger_state,
                                     assert, context.remote,
                                     config);
        // Run the ledger setup util. This compiles the declarative description
        // into a series of transactions and executes them.
        ledger.setup(lodash.noop /*logger*/, function(){
          done();
        })
      });
    });

    suiteTeardown(function(done) {
      testutils.build_teardown().call(context, done);
    });

    lodash.forOwn(tests, function(func, name) {
      test(name, function(done) {
        func.call(this, context.remote, context, done);
      });
    });
  });
}

makeSuite (
  'account_offers',
  {
    accounts: {
      G1 : {balance: ["1000.0"]},

      bob : {
        balance: ["1000.0", "1000/USD/G1"],
        // these offers will be in `Sequence`
        offers: [["100.0", "1/USD/bob"],
                 ["100.0", "1/USD/G1"],
                 ["10.0", "2/USD/G1"]]
      }
    }
  },
  {
    quality: function(remote, _, done) {
      remote.requestAccountOffers({account: 'bob'}, function(err, response) {
        var expected = [
           {"flags": 65536,
            "quality": "100000000",
            "seq": 3,
            "taker_gets": {"currency": "USD",
                           "issuer": "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
                           "value": "1"},
            "taker_pays": "100000000"},
           {"flags": 65536,
            "quality": "100000000",
            "seq": 4,
            "taker_gets": {"currency": "USD",
                           "issuer": "r32rQHyesiTtdWFU7UJVtff4nCR5SHCbJW",
                           "value": "1"},
            "taker_pays": "100000000"},
           {"flags": 65536,
            "quality": "5000000",
            "seq": 5,
            "taker_gets": {"currency": "USD",
                           "issuer": "r32rQHyesiTtdWFU7UJVtff4nCR5SHCbJW",
                           "value": "2"},
            "taker_pays": "10000000"}];

        assert.deepEqual(response.offers, expected);
        done();
      });
  }}
);
