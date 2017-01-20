var async     = require("async");
var assert    = require('assert');
var Remote    = require("ripple-lib").Remote;
var testutils = require("./testutils");
var config    = testutils.init_config();

suite('Monitor account', function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test('monitor root', function() {

    var self = this;

    var steps = [
      function (callback) {
        self.what = "Create accounts.";
        testutils.create_accounts($.remote, "root", "10000", ["alice"], callback);
      },

      function (callback) {
        self.what = "Close ledger.";
        $.remote.once('ledger_closed', function () {
          callback();
        });
        $.remote.ledger_accept();
      },

      function (callback) {
        self.what = "Dumping root.";

        testutils.account_dump($.remote, "root", function (error) {
          assert.ifError(error);
          callback();
        });
      }
    ]

    async.waterfall(steps, function(error) {
      assert(!effor, self.what);
      done();
    });
  });
});

// vim:sw=2:sts=2:ts=8:et
