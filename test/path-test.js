var async     = require("async");
var buster    = require("buster");

var Amount    = require("../src/js/amount.js").Amount;
var Remote    = require("../src/js/remote.js").Remote;
var Server    = require("./server.js").Server;

var testutils = require("./testutils.js");

require("../src/js/amount.js").config = require("./config.js");
require("../src/js/remote.js").config = require("./config.js");

buster.testRunner.timeout = 5000;

if (false)
buster.testCase("Basic Path finding", {
  // 'setUp' : testutils.build_setup({ verbose: true, no_server: true }),
  // 'setUp' : testutils.build_setup({ verbose: true }),
  'setUp' : testutils.build_setup(),
  'tearDown' : testutils.build_teardown(),

  "no direct path, no intermediary -> no alternatives" :
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000", ["alice", "bob"], callback);
          },
          function (callback) {
            self.what = "Find path from alice to bob";

            self.remote.request_ripple_path_find("alice", "bob", "5/USD/bob",
              [ { 'currency' : "USD" } ])
              .on('success', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  callback(m.alternatives.length);
                })
              .request();
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

  "direct path, no intermediary" :
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000", ["alice", "bob"], callback);
          },
          function (callback) {
            self.what = "Set credit limits.";

            testutils.credit_limits(self.remote,
              {
                "bob"   : "700/USD/alice",
              },
              callback);
          },
//          function (callback) {
//            self.what = "Display ledger";
//
//            self.remote.request_ledger('current', true)
//              .on('success', function (m) {
//                  console.log("Ledger: %s", JSON.stringify(m, undefined, 2));
//
//                  callback();
//                })
//              .request();
//          },
//          function (callback) {
//            self.what = "Display available lines from alice";
//
//            self.remote.request_account_lines("alice", undefined, 'CURRENT')
//              .on('success', function (m) {
//                  console.log("LINES: %s", JSON.stringify(m, undefined, 2));
//
//                  callback();
//                })
//              .request();
//          },
          function (callback) {
            self.what = "Find path from alice to bob";

            self.remote.request_ripple_path_find("alice", "bob", "5/USD/bob",
              [ { 'currency' : "USD" } ])
              .on('success', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  // 1 alternative.
                  buster.assert.equals(1, m.alternatives.length)
                  // Path is empty.
                  buster.assert.equals(0, m.alternatives[0].paths_canonical.length)

                  callback();
                })
              .request();
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

  "payment auto path find (using build_path)" :
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000", ["alice", "bob", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set credit limits.";

            testutils.credit_limits(self.remote,
              {
                "alice" : "600/USD/mtgox",
                "bob"   : "700/USD/mtgox",
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments(self.remote,
              {
                "mtgox" : [ "70/USD/alice" ],
              },
              callback);
          },
          function (callback) {
            self.what = "Payment with auto path";

            self.remote.transaction()
              .payment('alice', 'bob', "24/USD/bob")
              .build_path(true)
              .once('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances(self.remote,
              {
                "alice"   : "46/USD/mtgox",
                "mtgox"   : [ "-46/USD/alice", "-24/USD/bob" ],
                "bob"     : "24/USD/mtgox",
              },
              callback);
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

  "path find" :
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000", ["alice", "bob", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set credit limits.";

            testutils.credit_limits(self.remote,
              {
                "alice" : "600/USD/mtgox",
                "bob"   : "700/USD/mtgox",
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments(self.remote,
              {
                "mtgox" : [ "70/USD/alice", "50/USD/bob" ],
              },
              callback);
          },
          function (callback) {
            self.what = "Find path from alice to mtgox";

            self.remote.request_ripple_path_find("alice", "bob", "5/USD/mtgox",
              [ { 'currency' : "USD" } ])
              .on('success', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  // 1 alternative.
                  buster.assert.equals(1, m.alternatives.length)
                  // Path is empty.
                  buster.assert.equals(0, m.alternatives[0].paths_canonical.length)

                  callback();
                })
              .request();
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },
});

buster.testCase("Extended Path finding", {
  // 'setUp' : testutils.build_setup({ verbose: true, no_server: true }),
  // 'setUp' : testutils.build_setup({ verbose: true }),
  'setUp' : testutils.build_setup(),
  'tearDown' : testutils.build_teardown(),

  "alternative paths - consume both" :
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000", ["alice", "bob", "mtgox", "bitstamp"], callback);
          },
          function (callback) {
            self.what = "Set credit limits.";

            testutils.credit_limits(self.remote,
              {
                "alice" : [ "600/USD/mtgox", "800/USD/bitstamp" ],
                "bob"   : [ "700/USD/mtgox", "900/USD/bitstamp" ]
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments(self.remote,
              {
                "bitstamp" : "70/USD/alice",
                "mtgox" : "70/USD/alice",
              },
              callback);
          },
          function (callback) {
            self.what = "Payment with auto path";

            self.remote.transaction()
              .payment('alice', 'bob', "140/USD/bob")
              .build_path(true)
              .once('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances(self.remote,
              {
                "alice"     : [ "0/USD/mtgox", "0/USD/bitstamp" ],
                "bob"       : [ "70/USD/mtgox", "70/USD/bitstamp" ],
                "bitstamp"  : [ "0/USD/alice", "-70/USD/bob" ],
                "mtgox"     : [ "0/USD/alice", "-70/USD/bob" ],
              },
              callback);
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

  "alternative paths - consume best transfer" :
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000", ["alice", "bob", "mtgox", "bitstamp"], callback);
          },
          function (callback) {
            self.what = "Set transfer rate.";

            self.remote.transaction()
              .account_set("bitstamp")
              .transfer_rate(1e9*1.1)
              .once('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Set credit limits.";

            testutils.credit_limits(self.remote,
              {
                "alice" : [ "600/USD/mtgox", "800/USD/bitstamp" ],
                "bob"   : [ "700/USD/mtgox", "900/USD/bitstamp" ]
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments(self.remote,
              {
                "bitstamp" : "70/USD/alice",
                "mtgox" : "70/USD/alice",
              },
              callback);
          },
          function (callback) {
            self.what = "Payment with auto path";

            self.remote.transaction()
              .payment('alice', 'bob', "70/USD/bob")
              .build_path(true)
              .once('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances(self.remote,
              {
                "alice"     : [ "0/USD/mtgox", "70/USD/bitstamp" ],
                "bob"       : [ "70/USD/mtgox", "0/USD/bitstamp" ],
                "bitstamp"  : [ "-70/USD/alice", "0/USD/bob" ],
                "mtgox"     : [ "0/USD/alice", "-70/USD/bob" ],
              },
              callback);
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

  "alternative paths - consume best transfer first" :
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000", ["alice", "bob", "mtgox", "bitstamp"], callback);
          },
          function (callback) {
            self.what = "Set transfer rate.";

            self.remote.transaction()
              .account_set("bitstamp")
              .transfer_rate(1e9*1.1)
              .once('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Set credit limits.";

            testutils.credit_limits(self.remote,
              {
                "alice" : [ "600/USD/mtgox", "800/USD/bitstamp" ],
                "bob"   : [ "700/USD/mtgox", "900/USD/bitstamp" ]
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments(self.remote,
              {
                "bitstamp" : "70/USD/alice",
                "mtgox" : "70/USD/alice",
              },
              callback);
          },
          function (callback) {
            self.what = "Payment with auto path";

            self.remote.transaction()
              .payment('alice', 'bob', "77/USD/bob")
              .build_path(true)
              .send_max("100/USD/alice")
              .once('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances(self.remote,
              {
                "alice"     : [ "0/USD/mtgox", "62.3/USD/bitstamp" ],
                "bob"       : [ "70/USD/mtgox", "7/USD/bitstamp" ],
                "bitstamp"  : [ "-62.3/USD/alice", "-7/USD/bob" ],
                "mtgox"     : [ "0/USD/alice", "-70/USD/bob" ],
              },
              callback);
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },
});

// vim:sw=2:sts=2:ts=8:et
