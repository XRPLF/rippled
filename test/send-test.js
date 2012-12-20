var async     = require("async");
var buster    = require("buster");

var Amount    = require("../src/js/amount.js").Amount;
var Remote    = require("../src/js/remote.js").Remote;
var Server    = require("./server.js").Server;

var testutils = require("./testutils.js");

require("../src/js/amount.js").config = require("./config.js");
require("../src/js/remote.js").config = require("./config.js");

// How long to wait for server to start.
var serverDelay = 1500;

buster.testRunner.timeout = 5000;

/*
buster.testCase("Fee Changes", {
  'setUp' : testutils.build_setup({no_server: true}),  //
  'tearDown' : testutils.build_teardown(),

  "varying the fee for Payment" :
    function (done) {

                this.remote.transaction()
        .payment('root', 'alice', "10000")
        .on('success', function (r) {
           done();
          }).submit();

          this.remote.transaction()
        .payment('root', 'alice', "20000")
        .on('success', function (r) {
           done();
          }).submit();

           }
    });
  */

buster.testCase("Sending", {
  'setUp' : testutils.build_setup(),  //
  'tearDown' : testutils.build_teardown(),

  "send XRP to non-existent account with insufficent fee" : // => to run only that.
    function (done) {
      var self    = this;
      var ledgers = 20;
      var got_proposed;

      this.remote.transaction()
        .payment('root', 'alice', "1")
        .on('success', function (r) {
            // Transaction sent.

            // console.log("success: %s", JSON.stringify(r));
          })
        .on('pending', function() {
            // Moving ledgers along.
            // console.log("missing: %d", ledgers);

            ledgers    -= 1;
            if (ledgers) {
              self.remote.ledger_accept();
            }
            else {
              buster.assert(false, "Final never received.");
              done();
            }
          })
        .on('lost', function () {
            // Transaction did not make it in.
            // console.log("lost");

            buster.assert(true);
            done();
          })
        .on('proposed', function (m) {
            // Transaction got an error.
            // console.log("proposed: %s", JSON.stringify(m));

            buster.assert.equals(m.result, 'terNO_DST_INSUF_XRP');

            got_proposed  = true;

            self.remote.ledger_accept();    // Move it along.
          })
        .on('final', function (m) {
            // console.log("final: %s", JSON.stringify(m));

            buster.assert(false, "Should not have got a final.");
            done();
          })
        .on('error', function(m) {
            // console.log("error: %s", m);

            buster.assert(false);
          })
        .submit();
    },

  // Also test transaction becomes lost after terNO_DST.
  "credit_limit to non-existent account = terNO_DST" :
    function (done) {
      this.remote.transaction()
        .ripple_line_set("root", "100/USD/alice")
        .on('proposed', function (m) {
            //console.log("proposed: %s", JSON.stringify(m));

            buster.assert.equals(m.result, 'terNO_DST');

            done();
          })
        .submit();
    },

  "credit_limit" :
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Check a non-existent credit limit.";

            self.remote.request_ripple_balance("alice", "mtgox", "USD", 'CURRENT')
              .on('ripple_state', function (m) {
                  callback(true);
                })
              .on('error', function(m) {
                  // console.log("error: %s", JSON.stringify(m));

                  buster.assert.equals('remoteError', m.error);
                  buster.assert.equals('entryNotFound', m.remote.error);
                  callback();
                })
              .request();
          },
          function (callback) {
            self.what = "Create a credit limit.";

            testutils.credit_limit(self.remote, "alice", "800/USD/mtgox", callback);
          },
          function (callback) {
            self.remote.request_ripple_balance("alice", "mtgox", "USD", 'CURRENT')
              .on('ripple_state', function (m) {
//                console.log("BALANCE: %s", JSON.stringify(m));
//                console.log("account_balance: %s", m.account_balance.to_text_full());
//                console.log("account_limit: %s", m.account_limit.to_text_full());
//                console.log("issuer_balance: %s", m.issuer_balance.to_text_full());
//                console.log("issuer_limit: %s", m.issuer_limit.to_text_full());
                  buster.assert(m.account_balance.equals("0/USD/alice"));
                  buster.assert(m.account_limit.equals("800/USD/alice"));
                  buster.assert(m.issuer_balance.equals("0/USD/mtgox"));
                  buster.assert(m.issuer_limit.equals("0/USD/mtgox"));

                  callback();
                })
              .request();
          },
          function (callback) {
            self.what = "Modify a credit limit.";

            testutils.credit_limit(self.remote, "alice", "700/USD/mtgox", callback);
          },
          function (callback) {
            self.remote.request_ripple_balance("alice", "mtgox", "USD", 'CURRENT')
              .on('ripple_state', function (m) {
                  buster.assert(m.account_balance.equals("0/USD/alice"));
                  buster.assert(m.account_limit.equals("700/USD/alice"));
                  buster.assert(m.issuer_balance.equals("0/USD/mtgox"));
                  buster.assert(m.issuer_limit.equals("0/USD/mtgox"));

                  callback();
                })
              .request();
          },
          // Set negative limit.
          function (callback) {
            self.remote.transaction()
              .ripple_line_set("alice", "-1/USD/mtgox")
              .on('proposed', function (m) {
                  buster.assert.equals('temBAD_AMOUNT', m.result);

                  // After a malformed transaction, need to recover correct sequence.
                  self.remote.set_account_seq("alice", self.remote.account_seq("alice")-1);
                  callback('temBAD_AMOUNT' !== m.result);
                })
              .submit();
          },
          function (callback) {
            self.what = "Zero a credit limit.";

            testutils.credit_limit(self.remote, "alice", "0/USD/mtgox", callback);
          },
          function (callback) {
            self.what = "Make sure line is deleted.";

            self.remote.request_ripple_balance("alice", "mtgox", "USD", 'CURRENT')
              .on('ripple_state', function (m) {
                  // Used to keep lines.
                  // buster.assert(m.account_balance.equals("0/USD/alice"));
                  // buster.assert(m.account_limit.equals("0/USD/alice"));
                  // buster.assert(m.issuer_balance.equals("0/USD/mtgox"));
                  // buster.assert(m.issuer_limit.equals("0/USD/mtgox"));

                  buster.assert(false);
                })
              .on('error', function (m) {
                  // console.log("error: %s", JSON.stringify(m));
                  buster.assert.equals('remoteError', m.error);
                  buster.assert.equals('entryNotFound', m.remote.error);

                  callback();
                })
              .request();
          },
          // TODO Check in both owner books.
          function (callback) {
            self.what = "Set another limit.";

            testutils.credit_limit(self.remote, "alice", "600/USD/bob", callback);
          },
          function (callback) {
            self.what = "Set limit on other side.";

            testutils.credit_limit(self.remote, "bob", "500/USD/alice", callback);
          },
          function (callback) {
            self.what = "Check ripple_line's state from alice's pov.";

            self.remote.request_ripple_balance("alice", "bob", "USD", 'CURRENT')
              .on('ripple_state', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  buster.assert(m.account_balance.equals("0/USD/alice"));
                  buster.assert(m.account_limit.equals("600/USD/alice"));
                  buster.assert(m.issuer_balance.equals("0/USD/bob"));
                  buster.assert(m.issuer_limit.equals("500/USD/bob"));

                  callback();
                })
              .request();
          },
          function (callback) {
            self.what = "Check ripple_line's state from bob's pov.";

            self.remote.request_ripple_balance("bob", "alice", "USD", 'CURRENT')
              .on('ripple_state', function (m) {
                  buster.assert(m.account_balance.equals("0/USD/bob"));
                  buster.assert(m.account_limit.equals("500/USD/bob"));
                  buster.assert(m.issuer_balance.equals("0/USD/alice"));
                  buster.assert(m.issuer_limit.equals("600/USD/alice"));

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

// XXX In the future add ledger_accept after partial retry is implemented in the server.
buster.testCase("Sending future", {
  'setUp'     : testutils.build_setup(),
  // 'setUp'     : testutils.build_setup({ verbose : true }),
  'tearDown'  : testutils.build_teardown(),

  "direct ripple" :
    function (done) {
      var self = this;

      // self.remote.set_trace();

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob"], callback);
          },
          function (callback) {
            self.what = "Set alice's limit.";

            testutils.credit_limit(self.remote, "alice", "600/USD/bob", callback);
          },
          function (callback) {
            self.what = "Set bob's limit.";

            testutils.credit_limit(self.remote, "bob", "700/USD/alice", callback);
          },
          function (callback) {
            self.what = "Set alice send bob partial with alice as issuer.";

            self.remote.transaction()
              .payment('alice', 'bob', "24/USD/alice")
              .once('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');
                })
              .once('final', function (m) {
                  buster.assert(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balance.";

            self.remote.request_ripple_balance("alice", "bob", "USD", 'CURRENT')
              .once('ripple_state', function (m) {
                  buster.assert(m.account_balance.equals("-24/USD/alice"));
                  buster.assert(m.issuer_balance.equals("24/USD/bob"));

                  callback();
                })
              .request();
          },
          function (callback) {
            self.what = "Set alice send bob more with bob as issuer.";

            self.remote.transaction()
              .payment('alice', 'bob', "33/USD/bob")
              .once('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');
                })
              .once('final', function (m) {
                  buster.assert(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balance from bob's pov.";

            self.remote.request_ripple_balance("bob", "alice", "USD", 'CURRENT')
              .once('ripple_state', function (m) {
                  buster.assert(m.account_balance.equals("57/USD/bob"));
                  buster.assert(m.issuer_balance.equals("-57/USD/alice"));

                  callback();
                })
              .request();
          },
          function (callback) {
            self.what = "Bob send back more than sent.";

            self.remote.transaction()
              .payment('bob', 'alice', "90/USD/bob")
              .once('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');
                })
              .once('final', function (m) {
                  buster.assert(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balance from alice's pov.";

            self.remote.request_ripple_balance("alice", "bob", "USD", 'CURRENT')
              .once('ripple_state', function (m) {
                  buster.assert(m.account_balance.equals("33/USD/alice"));

                  callback();
                })
              .request();
          },
          function (callback) {
            self.what = "Alice send to limit.";

            self.remote.transaction()
              .payment('alice', 'bob', "733/USD/bob")
              .once('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');
                })
              .once('final', function (m) {
                  buster.assert(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balance from alice's pov.";

            self.remote.request_ripple_balance("alice", "bob", "USD", 'CURRENT')
              .once('ripple_state', function (m) {
                  buster.assert(m.account_balance.equals("-700/USD/alice"));

                  callback();
                })
              .request();
          },
          function (callback) {
            self.what = "Bob send to limit.";

            self.remote.transaction()
              .payment('bob', 'alice', "1300/USD/bob")
              .once('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');
                })
              .once('final', function (m) {
                  buster.assert(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balance from alice's pov.";

            self.remote.request_ripple_balance("alice", "bob", "USD", 'CURRENT')
              .once('ripple_state', function (m) {
                  buster.assert(m.account_balance.equals("600/USD/alice"));

                  callback();
                })
              .request();
          },
          function (callback) {
            // If this gets applied out of order, it could stop the big payment.
            self.what = "Bob send past limit.";

            self.remote.transaction()
              .payment('bob', 'alice', "1/USD/bob")
              .once('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));
                  callback(m.result !== 'tepPATH_DRY');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balance from alice's pov.";

            self.remote.request_ripple_balance("alice", "bob", "USD", 'CURRENT')
              .once('ripple_state', function (m) {
                  buster.assert(m.account_balance.equals("600/USD/alice"));

                  callback();
                })
              .request();
          },
//        function (callback) {
//          // Make sure all is good after canonical ordering.
//          self.what = "Close the ledger and check balance.";
//
//          self.remote
//            .once('ledger_closed', function (message) {
//                // console.log("LEDGER_CLOSED: A: %d: %s", ledger_closed_index, ledger_closed);
//                callback();
//              })
//            .ledger_accept();
//        },
//        function (callback) {
//          self.what = "Verify balance from alice's pov.";
//
//          self.remote.request_ripple_balance("alice", "bob", "USD", 'CURRENT')
//            .once('ripple_state', function (m) {
//                console.log("account_balance: %s", m.account_balance.to_text_full());
//                console.log("account_limit: %s", m.account_limit.to_text_full());
//                console.log("issuer_balance: %s", m.issuer_balance.to_text_full());
//                console.log("issuer_limit: %s", m.issuer_limit.to_text_full());
//
//                buster.assert(m.account_balance.equals("600/USD/alice"));
//
//                callback();
//              })
//            .request();
//        },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

    // Ripple without credit path.
    // Ripple with one-way credit path.
});

buster.testCase("Indirect ripple", {
  // 'setUp'     : testutils.build_setup({ verbose: true }),
  'setUp'     : testutils.build_setup(),
  'tearDown'  : testutils.build_teardown(),

  "indirect ripple" :
    function (done) {
      var self = this;

      // self.remote.set_trace();

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
            self.what = "Verify alice balance with mtgox.";

            testutils.verify_balance(self.remote, "alice", "70/USD/mtgox", callback);
          },
          function (callback) {
            self.what = "Verify bob balance with mtgox.";

            testutils.verify_balance(self.remote, "bob", "50/USD/mtgox", callback);
          },
          function (callback) {
            self.what = "Alice sends more than has to issuer: 100 out of 70";

            self.remote.transaction()
              .payment("alice", "mtgox", "100/USD/mtgox")
              .on('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  callback(m.result !== 'tepPATH_PARTIAL');
                })
              .submit();
          },
          function (callback) {
            self.what = "Alice sends more than has to bob: 100 out of 70";

            self.remote.transaction()
              .payment("alice", "bob", "100/USD/mtgox")
              .on('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  callback(m.result !== 'tepPATH_PARTIAL');
                })
              .submit();
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

  "indirect ripple with path" :
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
            self.what = "Alice sends via a path";

            self.remote.transaction()
              .payment("alice", "bob", "5/USD/mtgox")
              .path_add( [ { account: "mtgox" } ])
              .on('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify alice balance with mtgox.";

            testutils.verify_balance(self.remote, "alice", "65/USD/mtgox", callback);
          },
          function (callback) {
            self.what = "Verify bob balance with mtgox.";

            testutils.verify_balance(self.remote, "bob", "55/USD/mtgox", callback);
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

  "indirect ripple with multi path" :
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "carol", "amazon", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set credit limits.";

            testutils.credit_limits(self.remote,
              {
                "amazon"  : "2000/USD/mtgox",
                "bob"   : [ "600/USD/alice", "1000/USD/mtgox" ],
                "carol" : [ "700/USD/alice", "1000/USD/mtgox" ],
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments(self.remote,
              {
                "mtgox" : [ "100/USD/bob", "100/USD/carol" ],
              },
              callback);
          },
          function (callback) {
            self.what = "Alice pays amazon via multiple paths";

            self.remote.transaction()
              .payment("alice", "amazon", "150/USD/mtgox")
              .path_add( [ { account: "bob" } ])
              .path_add( [ { account: "carol" } ])
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
                "alice"   : [ "-100/USD/bob", "-50/USD/carol" ],
                "amazon"  : "150/USD/mtgox",
                "bob"     : "0/USD/mtgox",
                "carol"   : "50/USD/mtgox",
              },
              callback);
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

  "indirect ripple with path and transfer fee" :
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "carol", "amazon", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set mtgox transfer rate.";

            testutils.transfer_rate(self.remote, "mtgox", 1.1e9, callback);
          },
          function (callback) {
            self.what = "Set credit limits.";

            testutils.credit_limits(self.remote,
              {
                "amazon"  : "2000/USD/mtgox",
                "bob"   : [ "600/USD/alice", "1000/USD/mtgox" ],
                "carol" : [ "700/USD/alice", "1000/USD/mtgox" ],
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments(self.remote,
              {
                "mtgox" : [ "100/USD/bob", "100/USD/carol" ],
              },
              callback);
          },
          function (callback) {
            self.what = "Alice pays amazon via multiple paths";

            self.remote.transaction()
              .payment("alice", "amazon", "150/USD/mtgox")
              .send_max("200/USD/alice")
              .path_add( [ { account: "bob" } ])
              .path_add( [ { account: "carol" } ])
              .on('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
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
            self.what = "Verify balances.";

            // 65.00000000000001 is correct.
            // This is result of limited precision.
            testutils.verify_balances(self.remote,
              {
                "alice"   : [ "-100/USD/bob", "-65.00000000000001/USD/carol" ],
                "amazon"  : "150/USD/mtgox",
                "bob"     : "0/USD/mtgox",
                "carol"   : "35/USD/mtgox",
              },
              callback);
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },
    // Direct ripple without no liqudity.
    // Test with XRP at start and end.
});

// vim:sw=2:sts=2:ts=8:et
