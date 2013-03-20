var async       = require("async");
var buster      = require("buster");

var Amount      = require("../src/js/amount.js").Amount;
var Remote      = require("../src/js/remote.js").Remote;
var Transaction = require("../src/js/transaction.js").Transaction;
var Server      = require("./server.js").Server;

var testutils = require("./testutils.js");

require('../src/js/config').load(require('./config'));

buster.testRunner.timeout = 5000;

buster.testCase("Basic Path finding", {
  'setUp' : testutils.build_setup(),
  // 'setUp' : testutils.build_setup({ verbose: true }),
  // 'setUp' : testutils.build_setup({ verbose: true, no_server: true }),
  'tearDown' : testutils.build_teardown(),

  "no direct path, no intermediary -> no alternatives" :
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob"], callback);
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

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob"], callback);
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

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "mtgox"], callback);
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

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "mtgox"], callback);
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

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "mtgox", "bitstamp"], callback);
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

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "mtgox", "bitstamp"], callback);
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

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "mtgox", "bitstamp"], callback);
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

    // Test alternative paths with qualities.
});

buster.testCase("More Path finding", {
  // 'setUp' : testutils.build_setup({ verbose: true, no_server: true }),
  // 'setUp' : testutils.build_setup({ verbose: true }),
  'setUp' : testutils.build_setup(),
  'tearDown' : testutils.build_teardown(),

  "// alternative paths - limit returned paths to best quality" :
    // alice +- bitstamp         -+ bob
    //       |- carol(fee)       -|     // To be excluded.
    //       |- dan(issue)       -|
    //       |- mtgox            -|
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "carol", "dan", "mtgox", "bitstamp"], callback);
          },
          function (callback) {
            self.what = "Set transfer rate.";

            self.remote.transaction()
              .account_set("carol")
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
                "alice" : [ "800/USD/bitstamp", "800/USD/carol", "800/USD/dan", "800/USD/mtgox", ],
                "bob"   : [ "800/USD/bitstamp", "800/USD/carol", "800/USD/dan", "800/USD/mtgox", ],
                "dan"   : [ "800/USD/alice", "800/USD/bob" ],
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments(self.remote,
              {
                "bitstamp" : "100/USD/alice",
                "carol" : "100/USD/alice",
                "mtgox" : "100/USD/alice",
              },
              callback);
          },
// XXX What should this check?
          function (callback) {
            self.what = "Find path from alice to bob";

            self.remote.request_ripple_path_find("alice", "bob", "5/USD/bob",
              [ { 'currency' : "USD" } ])
              .on('success', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  // 1 alternative.
//                  buster.assert.equals(1, m.alternatives.length)
//                  // Path is empty.
//                  buster.assert.equals(0, m.alternatives[0].paths_canonical.length)

                  callback();
                })
              .request();
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

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "mtgox", "bitstamp"], callback);
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

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "mtgox", "bitstamp"], callback);
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

    // Test alternative paths with qualities.
});

buster.testCase("Issues", {
  // 'setUp' : testutils.build_setup({ verbose: true, no_server: true }),
  // 'setUp' : testutils.build_setup({ verbose: true }),
  'setUp' : testutils.build_setup(),
  'tearDown' : testutils.build_teardown(),

  "Path negative: Issue #5" :
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "carol", "dan"], callback);
          },
          function (callback) {
            self.what = "Set credit limits.";

            testutils.credit_limits(self.remote,
              {
                //  2. acct 4 trusted all the other accts for 100 usd
                "dan"   : [ "100/USD/alice", "100/USD/bob", "100/USD/carol" ],
                //  3. acct 2 acted as a nexus for acct 1 and 3, was trusted by 1 and 3 for 100 usd
                "alice" : [ "100/USD/bob" ],
                "carol" : [ "100/USD/bob" ],
              },
              callback);
          },
          function (callback) {
            // 4. acct 2 sent acct 3 a 75 iou
            self.what = "Bob sends Carol 75.";

            self.remote.transaction()
              .payment("bob", "carol", "75/USD/bob")
              .on('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances(self.remote,
              {
                "bob"   : [ "-75/USD/carol" ],
                "carol"   : "75/USD/bob",
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
          function (callback) {
            self.what = "Find path from alice to bob";

            // 5. acct 1 sent a 25 usd iou to acct 2
            self.remote.request_ripple_path_find("alice", "bob", "25/USD/bob",
              [ { 'currency' : "USD" } ])
              .on('success', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  // 0 alternatives.
                  buster.assert.equals(0, m.alternatives.length)

                  callback();
                })
              .request();
          },
          function (callback) {
            self.what = "alice fails to send to bob.";

            self.remote.transaction()
              .payment('alice', 'bob', "25/USD/alice")
              .once('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));
                  callback(m.result !== 'tecPATH_DRY');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balances final.";

            testutils.verify_balances(self.remote,
              {
                "alice" : [ "0/USD/bob", "0/USD/dan"],
                "bob"   : [ "0/USD/alice", "-75/USD/carol", "0/USD/dan" ],
                "carol" : [ "75/USD/bob", "0/USD/dan" ],
                "dan" : [ "0/USD/alice", "0/USD/bob", "0/USD/carol" ],
              },
              callback);
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

  "Path negative: ripple-client issue #23: smaller" :
    //
    // alice -- limit 40 --> bob
    // alice --> carol --> dan --> bob
    // Balance of 100 USD Bob - Balance of 37 USD -> Rod
    //
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "carol", "dan"], callback);
          },
          function (callback) {
            self.what = "Set credit limits.";

            testutils.credit_limits(self.remote,
              {
                "bob"   : [ "40/USD/alice", "20/USD/dan" ],
                "carol" : [ "20/USD/alice" ],
                "dan"   : [ "20/USD/carol" ],
              },
              callback);
          },
          function (callback) {
            self.what = "Payment.";

            self.remote.transaction()
              .payment('alice', 'bob', "55/USD/bob")
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
                "bob"   : [ "40/USD/alice", "15/USD/dan" ],
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
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

  "Path negative: ripple-client issue #23: larger" :
    //
    // alice -120 USD-> amazon -25 USD-> bob
    // alice -25 USD-> carol -75 USD -> dan -100 USD-> bob
    //
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "carol", "dan", "amazon"], callback);
          },
          function (callback) {
            self.what = "Set credit limits.";

            testutils.credit_limits(self.remote,
              {
                "amazon"  : [ "120/USD/alice" ],
                "bob"     : [ "25/USD/amazon", "100/USD/dan" ],
                "carol"   : [ "25/USD/alice" ],
                "dan"     : [ "75/USD/carol" ],
              },
              callback);
          },
          function (callback) {
            self.what = "Payment.";

            self.remote.transaction()
              .payment('alice', 'bob', "50/USD/bob")
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
                "alice"   : [ "-25/USD/amazon", "-25/USD/carol" ],
                "bob"     : [ "25/USD/amazon", "25/USD/dan" ],
                "carol"   : [ "25/USD/alice", "-25/USD/dan" ],
                "dan"     : [ "25/USD/carol", "-25/USD/bob" ],
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
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    }
});

buster.testCase("Via offers", {
  'setUp' : testutils.build_setup(),
  // 'setUp' : testutils.build_setup({ verbose: true }),
  // 'setUp' : testutils.build_setup({ verbose: true, no_server: true }),
  'tearDown' : testutils.build_teardown(),

  "Via gateway" :
    // carol holds mtgoxAUD, sells mtgoxAUD for XRP
    // bob will hold mtgoxAUD
    // alice pays bob mtgoxAUD using XRP
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "carol", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set transfer rate.";

            self.remote.transaction()
              .account_set("mtgox")
              .transfer_rate(1005000000)
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
                "bob" : [ "100/AUD/mtgox" ],
                "carol" : [ "100/AUD/mtgox" ],
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments(self.remote,
              {
                "mtgox" : "50/AUD/carol",
              },
              callback);
          },
          function (callback) {
            self.what = "Carol create offer.";

            self.remote.transaction()
              .offer_create("carol", "50.0", "50/AUD/mtgox")
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');

                  seq_carol = m.tx_json.Sequence;
                })
              .submit();
          },
          function (callback) {
            self.what = "Alice sends bob 10/AUD/mtgox using XRP.";

            // XXX Also try sending 10/AUX/bob
            self.remote.transaction()
              .payment("alice", "bob", "10/AUD/mtgox")
              .build_path(true)
              .send_max("100.0")
              .on('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances(self.remote,
              {
                "bob"   : "10/AUD/mtgox",
                "carol"   : "39.95/AUD/mtgox",
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
//            self.what = "Find path from alice to bob";
//
//            // 5. acct 1 sent a 25 usd iou to acct 2
//            self.remote.request_ripple_path_find("alice", "bob", "25/USD/bob",
//              [ { 'currency' : "USD" } ])
//              .on('success', function (m) {
//                  // console.log("proposed: %s", JSON.stringify(m));
//
//                  // 0 alternatives.
//                  buster.assert.equals(0, m.alternatives.length)
//
//                  callback();
//                })
//              .request();
//          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

  "// Via gateway : FIX ME fails due to XRP rounding and not properly handling dry." :
    // carol holds mtgoxAUD, sells mtgoxAUD for XRP
    // bob will hold mtgoxAUD
    // alice pays bob mtgoxAUD using XRP
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "carol", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set transfer rate.";

            self.remote.transaction()
              .account_set("mtgox")
              .transfer_rate(1005000000)
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
                "bob" : [ "100/AUD/mtgox" ],
                "carol" : [ "100/AUD/mtgox" ],
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments(self.remote,
              {
                "mtgox" : "50/AUD/carol",
              },
              callback);
          },
          function (callback) {
            self.what = "Carol create offer.";

            self.remote.transaction()
              .offer_create("carol", "50", "50/AUD/mtgox")
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');

                  seq_carol = m.tx_json.Sequence;
                })
              .submit();
          },
          function (callback) {
            self.what = "Alice sends bob 10/AUD/mtgox using XRP.";

            // XXX Also try sending 10/AUX/bob
            self.remote.transaction()
              .payment("alice", "bob", "10/AUD/mtgox")
              .build_path(true)
              .send_max("100")
              .on('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances(self.remote,
              {
                "bob"   : "10/AUD/mtgox",
                "carol"   : "39.95/AUD/mtgox",
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
//            self.what = "Find path from alice to bob";
//
//            // 5. acct 1 sent a 25 usd iou to acct 2
//            self.remote.request_ripple_path_find("alice", "bob", "25/USD/bob",
//              [ { 'currency' : "USD" } ])
//              .on('success', function (m) {
//                  // console.log("proposed: %s", JSON.stringify(m));
//
//                  // 0 alternatives.
//                  buster.assert.equals(0, m.alternatives.length)
//
//                  callback();
//                })
//              .request();
//          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },
});

buster.testCase("Indirect paths", {
  'setUp' : testutils.build_setup(),
  // 'setUp' : testutils.build_setup({ verbose: true }),
  // 'setUp' : testutils.build_setup({ verbose: true, no_server: true }),
  'tearDown' : testutils.build_teardown(),

  "path find" :
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "carol"], callback);
          },
          function (callback) {
            self.what = "Set credit limits.";

            testutils.credit_limits(self.remote,
              {
                "bob"   : "1000/USD/alice",
                "carol" : "2000/USD/bob",
              },
              callback);
          },
          function (callback) {
            self.what = "Find path from alice to carol";

            self.remote.request_ripple_path_find("alice", "carol", "5/USD/carol",
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

buster.testCase("Quality paths", {
  'setUp' : testutils.build_setup(),
  // 'setUp' : testutils.build_setup({ verbose: true }),
  // 'setUp' : testutils.build_setup({ verbose: true, no_server: true }),
  'tearDown' : testutils.build_teardown(),

  "quality set and test" :
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob"], callback);
          },
          function (callback) {
            self.what = "Set credit limits extended.";

            testutils.credit_limits(self.remote,
              {
                "bob"   : "1000/USD/alice:2000,1400000000",
              },
              callback);
          },
          function (callback) {
            self.what = "Verify credit limits extended.";

            testutils.verify_limit(self.remote, "bob", "1000/USD/alice:2000,1400000000", callback);
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

  "// quality payment (BROKEN DUE TO ROUNDING)" :
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob"], callback);
          },
          function (callback) {
            self.what = "Set credit limits extended.";

            testutils.credit_limits(self.remote,
              {
                "bob"   : "1000/USD/alice:" + .9*1e9 + "," + 1e9,
              },
              callback);
          },
          function (callback) {
            self.what = "Payment with auto path";

            self.remote.transaction()
              .payment('alice', 'bob', "100/USD/bob")
              .send_max("120/USD/alice")
//              .set_flags('PartialPayment')
//              .build_path(true)
              .once('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Display ledger";

            self.remote.request_ledger('current', { accounts: true, expand: true })
              .on('success', function (m) {
                  console.log("Ledger: %s", JSON.stringify(m, undefined, 2));

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
// vim:sw=2:sts=2:ts=8:et
