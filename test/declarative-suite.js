/* global test, suite, suiteSetup, suiteTeardown */

var lodash = require('lodash');
var testutils = require('./testutils');
var LedgerState = require('./ledger-state').LedgerState;
var assert = require('assert-diff');

var config = testutils.init_config();
// We just use equal instead of strictEqual everywhere.
assert.options.strict = true;

function makeSuite (name, ledger_state, tests) {
  suite(name, function () {
    // build_(setup|teardown) utils functions set state on this context var.
    var context = {};

    // This runs only once
    suiteSetup(function (done) {
      var opts = {};
      if (ledger_state.dump) {
        opts.ledger_file = ledger_state.dump;
      }
      testutils.build_setup(opts).call(context, function () {
        if (opts.ledger_file) {
          done();
        } else {
          var ledger = context.ledger = new LedgerState(ledger_state,
                                       assert, context.remote,
                                       config);
          // Run the ledger setup util. This compiles the declarative description
          // into a series of transactions and executes them.
          ledger.setup(lodash.noop/* logger */, function () {
            done();
          });
        }
      });
    });

    suiteTeardown(function (done) {
      testutils.build_teardown().call(context, done);
    });

    lodash.forOwn(tests, function (func, name) {
      test(name, function (done) {
        var args = [context, context.ledger, context.remote, done];
        while (args.length > func.length && args.length > 1) {
          args.shift();
        }
        func.apply(this, args);
      });
    });
  });
}

module.exports = {
  makeSuite: makeSuite
};
