
var async       = require("async");
var buster      = require("buster");

var Amount      = require("../src/js/amount").Amount;
var Remote      = require("../src/js/remote").Remote;
var Transaction = require("../src/js/transaction").Transaction;
var Server      = require("./server").Server;

var testutils = require("./testutils");

require('../src/js/config').load(require('./config'));

buster.testRunner.timeout = 5000;

buster.testCase("Offer tests", {
  'setUp'     : testutils.build_setup(),
  // 'setUp'     : testutils.build_setup({ verbose: true }),
  // 'setUp'     : testutils.build_setup({ verbose: true, standalone: true }),
  'tearDown'  : testutils.build_teardown(),

  "offer create then cancel in one ledger" :
    function (done) {
      var self = this;
      var final_create;

      async.waterfall([
          function (callback) {
            self.remote.transaction()
              .offer_create("root", "500", "100/USD/root")
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS', m);
                })
              .on('final', function (m) {
                  // console.log("FINAL: offer_create: %s", JSON.stringify(m));

                  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);

                  buster.assert(final_create);
                })
              .submit();
          },
          function (m, callback) {
            self.remote.transaction()
              .offer_cancel("root", m.tx_json.Sequence)
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_cancel: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS', m);
                })
              .on('final', function (m) {
                  // console.log("FINAL: offer_cancel: %s", JSON.stringify(m, undefined, 2));

                  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);
                  buster.assert(final_create);
                  done();
                })
              .submit();
          },
          function (m, callback) {
            self.remote
              .once('ledger_closed', function (message) {
                  // console.log("LEDGER_CLOSED: %d: %s", ledger_index, ledger_hash);
                  final_create  = message;
                })
              .ledger_accept();
          }
        ], function (error) {
          // console.log("result: error=%s", error);
          buster.refute(error);

          done();
        });
    },

  "Offer create then self crossing offer, no trust lines with self" :
    function (done) {
      var self = this;

      async.waterfall([
        function (callback) {
          self.what = "Create first offer.";

          self.remote.transaction()
            .offer_create("root", "500/BTC/root", "100/USD/root")
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
        },
        function (callback) {
          self.what = "Create crossing offer.";

          self.remote.transaction()
            .offer_create("root", "100/USD/root", "500/BTC/root")
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
        }
      ], function (error) {
        // console.log("result: error=%s", error);
        buster.refute(error, self.what);
        done();
      });
    },

  "Offer create then crossing offer with XRP. Negative balance." :
    function (done) {
      var self = this;

      async.waterfall([
        function (callback) {
          self.what = "Create mtgox account.";

          testutils.payment(self.remote, "root", "mtgox", "1149999730", callback);
        },
        function (callback) {
          self.what = "Create alice account.";

          testutils.payment(self.remote, "root", "alice", "499946999680", callback);
        },
        function (callback) {
          self.what = "Create bob account.";

          testutils.payment(self.remote, "root", "bob", "10199999920", callback);
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
          self.what = "Set limits.";

          testutils.credit_limits(self.remote,
            {
              "alice" : "500/USD/mtgox",
              "bob" : "50/USD/mtgox",
              "mtgox" : "100/USD/alice",
            },
            callback);
        },
        function (callback) {
          self.what = "Distribute funds.";

          testutils.payments(self.remote,
            {
              "mtgox" : [ "50/USD/alice", "2710505431213761e-33/USD/bob" ]
            },
            callback);
        },
        function (callback) {
          self.what = "Create first offer.";

          self.remote.transaction()
            .offer_create("alice", "50/USD/mtgox", "150000.0")    // get 50/USD pay 150000/XRP
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
        },
        function (callback) {
          self.what = "Unfund offer.";

          testutils.payments(self.remote,
            {
              "alice" : "100/USD/mtgox"
            },
            callback);
        },
        function (callback) {
          self.what = "Set limits 2.";

          testutils.credit_limits(self.remote,
            {
              "mtgox" : "0/USD/alice",
            },
            callback);
        },
        function (callback) {
          self.what = "Verify balances.";

          testutils.verify_balances(self.remote,
            {
              "alice"   : [ "-50/USD/mtgox" ],
              "bob"     : [ "2710505431213761e-33/USD/mtgox" ],
            },
            callback);
        },
        function (callback) {
          self.what = "Create crossing offer.";

          self.remote.transaction()
            .offer_create("bob", "2000.0", "1/USD/mtgox")  // get 2,000/XRP pay 1/USD (has insufficient USD)
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
        },
        function (callback) {
          self.what = "Verify balances.";

          testutils.verify_balances(self.remote,
            {
              "alice"   : [ "-50/USD/mtgox", String(499946999680-3*(Transaction.fees['default'].to_number())) ],
              "bob"     : [   "2710505431213761e-33/USD/mtgox", String(10199999920-2*(Transaction.fees['default'].to_number())) ],
            },
            callback);
        },
//        function (callback) {
//          self.what = "Display ledger";
//
//          self.remote.request_ledger('current', true)
//            .on('success', function (m) {
//                console.log("Ledger: %s", JSON.stringify(m, undefined, 2));
//
//                callback();
//              })
//            .request();
//        },
      ], function (error) {
        // console.log("result: error=%s", error);
        buster.refute(error, self.what);
        done();
      });
    },

  "Offer create then crossing offer with XRP. Reverse order." :
    function (done) {
      var self = this;

      async.waterfall([
        function (callback) {
          self.what = "Create accounts.";

          testutils.create_accounts(self.remote, "root", "100000.0", ["alice", "bob", "mtgox"], callback);
        },
        function (callback) {
          self.what = "Set limits.";

          testutils.credit_limits(self.remote,
            {
              "alice" : "1000/USD/mtgox",
              "bob" : "1000/USD/mtgox"
            },
            callback);
        },
        function (callback) {
          self.what = "Distribute funds.";

          testutils.payments(self.remote,
            {
              "mtgox" : "500/USD/alice"
            },
            callback);
        },
        function (callback) {
          self.what = "Create first offer.";

          self.remote.transaction()
            .offer_create("bob", "1/USD/mtgox", "4000.0")         // get 1/USD pay 4000/XRP : offer pays 4000 XRP for 1 USD
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
        },
        function (callback) {
          self.what = "Create crossing offer.";

          // Existing offer pays better than this wants.
          // Fully consume existing offer.
          // Pay 1 USD, get 4000 XRP.

          self.remote.transaction()
            .offer_create("alice", "150000.0", "50/USD/mtgox")  // get 150,000/XRP pay 50/USD : offer pays 1 USD for 3000 XRP
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
        },
        function (callback) {
          self.what = "Verify balances.";

          testutils.verify_balances(self.remote,
            {
              "alice"   : [ "499/USD/mtgox", String(100000000000+4000000000-2*(Transaction.fees['default'].to_number())) ],
              "bob"     : [   "1/USD/mtgox", String(100000000000-4000000000-2*(Transaction.fees['default'].to_number())) ],
            },
            callback);
        },
//        function (callback) {
//          self.what = "Display ledger";
//
//          self.remote.request_ledger('current', true)
//            .on('success', function (m) {
//                console.log("Ledger: %s", JSON.stringify(m, undefined, 2));
//
//                callback();
//              })
//            .request();
//        },
      ], function (error) {
        // console.log("result: error=%s", error);
        buster.refute(error, self.what);
        done();
      });
    },

  "Offer create then crossing offer with XRP." :
    function (done) {
      var self = this;

      async.waterfall([
        function (callback) {
          self.what = "Create accounts.";

          testutils.create_accounts(self.remote, "root", "100000.0", ["alice", "bob", "mtgox"], callback);
        },
        function (callback) {
          self.what = "Set limits.";

          testutils.credit_limits(self.remote,
            {
              "alice" : "1000/USD/mtgox",
              "bob" : "1000/USD/mtgox"
            },
            callback);
        },
        function (callback) {
          self.what = "Distribute funds.";

          testutils.payments(self.remote,
            {
              "mtgox" : "500/USD/alice"
            },
            callback);
        },
        function (callback) {
          self.what = "Create first offer.";

          self.remote.transaction()
            .offer_create("alice", "150000.0", "50/USD/mtgox")  // pays 1 USD for 3000 XRP
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
        },
        function (callback) {
          self.what = "Create crossing offer.";

          self.remote.transaction()
            .offer_create("bob", "1/USD/mtgox", "4000.0") // pays 4000 XRP for 1 USD
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
        },
        function (callback) {
          self.what = "Verify balances.";

          // New offer pays better than old wants.
          // Fully consume new offer.
          // Pay 1 USD, get 3000 XRP.

          testutils.verify_balances(self.remote,
            {
              "alice"   : [ "499/USD/mtgox", String(100000000000+3000000000-2*(Transaction.fees['default'].to_number())) ],
              "bob"     : [   "1/USD/mtgox", String(100000000000-3000000000-2*(Transaction.fees['default'].to_number())) ],
            },
            callback);
        },
//        function (callback) {
//          self.what = "Display ledger";
//
//          self.remote.request_ledger('current', true)
//            .on('success', function (m) {
//                console.log("Ledger: %s", JSON.stringify(m, undefined, 2));
//
//                callback();
//              })
//            .request();
//        },
      ], function (error) {
        // console.log("result: error=%s", error);
        buster.refute(error, self.what);
        done();
      });
    },

  "Offer create then crossing offer with XRP with limit override." :
    function (done) {
      var self = this;

      async.waterfall([
        function (callback) {
          self.what = "Create accounts.";

          testutils.create_accounts(self.remote, "root", "100000.0", ["alice", "bob", "mtgox"], callback);
        },
        function (callback) {
          self.what = "Set limits.";

          testutils.credit_limits(self.remote,
            {
              "alice" : "1000/USD/mtgox",
//              "bob" : "1000/USD/mtgox"
            },
            callback);
        },
        function (callback) {
          self.what = "Distribute funds.";

          testutils.payments(self.remote,
            {
              "mtgox" : "500/USD/alice"
            },
            callback);
        },
        function (callback) {
          self.what = "Create first offer.";

          self.remote.transaction()
            .offer_create("alice", "150000.0", "50/USD/mtgox")  // 300 XRP = 1 USD
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
        },
//        function (callback) {
//          self.what = "Display ledger";
//
//          self.remote.request_ledger('current', true)
//            .on('success', function (m) {
//                console.log("Ledger: %s", JSON.stringify(m, undefined, 2));
//
//                callback();
//              })
//            .request();
//        },
        function (callback) {
          self.what = "Create crossing offer.";

          self.remote.transaction()
            .offer_create("bob", "1/USD/mtgox", "3000.0") // 
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
        },
        function (callback) {
          self.what = "Verify balances.";

          testutils.verify_balances(self.remote,
            {
              "alice"   : [ "499/USD/mtgox", String(100000000000+3000000000-2*(Transaction.fees['default'].to_number())) ],
              "bob"     : [   "1/USD/mtgox", String(100000000000-3000000000-1*(Transaction.fees['default'].to_number())) ],
            },
            callback);
        },
//        function (callback) {
//          self.what = "Display ledger";
//
//          self.remote.request_ledger('current', true)
//            .on('success', function (m) {
//                console.log("Ledger: %s", JSON.stringify(m, undefined, 2));
//
//                callback();
//              })
//            .request();
//        },
      ], function (error) {
        // console.log("result: error=%s", error);
        buster.refute(error, self.what);
        done();
      });
    },

  "offer_create then ledger_accept then offer_cancel then ledger_accept." :
    function (done) {
      var self = this;
      var final_create;
      var offer_seq;

      async.waterfall([
          function (callback) {
            self.remote.transaction()
              .offer_create("root", "500", "100/USD/root")
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  offer_seq = m.tx_json.Sequence;

                  callback(m.result !== 'tesSUCCESS');
                })
              .on('final', function (m) {
                  // console.log("FINAL: offer_create: %s", JSON.stringify(m));

                  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);

                  final_create  = m;

                  callback();
                })
              .submit();
          },
          function (callback) {
            if (!final_create) {
              self.remote
                .once('ledger_closed', function (mesage) {
                    // console.log("LEDGER_CLOSED: %d: %s", ledger_index, ledger_hash);

                  })
                .ledger_accept();
            }
            else {
              callback();
            }
          },
          function (callback) {
            // console.log("CANCEL: offer_cancel: %d", offer_seq);

            self.remote.transaction()
              .offer_cancel("root", offer_seq)
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_cancel: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');
                })
              .on('final', function (m) {
                  // console.log("FINAL: offer_cancel: %s", JSON.stringify(m));

                  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);
                  buster.assert(final_create);

                  done();
                })
              .submit();
          },
          // See if ledger_accept will crash.
          function (callback) {
            self.remote
              .once('ledger_closed', function (mesage) {
                  // console.log("LEDGER_CLOSED: A: %d: %s", ledger_index, ledger_hash);
                  callback();
                })
              .ledger_accept();
          },
          function (callback) {
            self.remote
              .once('ledger_closed', function (mesage) {
                  // console.log("LEDGER_CLOSED: B: %d: %s", ledger_index, ledger_hash);
                  callback();
                })
              .ledger_accept();
          },
        ], function (error) {
          // console.log("result: error=%s", error);
          buster.refute(error);

          if (error) done();
        });
    },

  "//new user offer_create then ledger_accept then offer_cancel then ledger_accept." :
    function (done) {
      var self = this;
      var final_create;
      var offer_seq;

      async.waterfall([
          function (callback) {
            self.remote.transaction()
              .payment('root', 'alice', "1000")
              .on('proposed', function (m) {
                // console.log("proposed: %s", JSON.stringify(m));
                buster.assert.equals(m.result, 'tesSUCCESS');
                callback();
              })
              .submit()
          },
          function (callback) {
            self.remote.transaction()
              .offer_create("alice", "500", "100/USD/alice")
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  offer_seq = m.tx_json.Sequence;

                  callback(m.result !== 'tesSUCCESS');
                })
              .on('final', function (m) {
                  // console.log("FINAL: offer_create: %s", JSON.stringify(m));

                  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);

                  final_create  = m;

                  callback();
                })
              .submit();
          },
          function (callback) {
            if (!final_create) {
              self.remote
                .once('ledger_closed', function (mesage) {
                    // console.log("LEDGER_CLOSED: %d: %s", ledger_index, ledger_hash);

                  })
                .ledger_accept();
            }
            else {
              callback();
            }
          },
          function (callback) {
            // console.log("CANCEL: offer_cancel: %d", offer_seq);

            self.remote.transaction()
              .offer_cancel("alice", offer_seq)
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_cancel: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');
                })
              .on('final', function (m) {
                  // console.log("FINAL: offer_cancel: %s", JSON.stringify(m));

                  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);
                  buster.assert(final_create);

                  done();
                })
              .submit();
          },
          // See if ledger_accept will crash.
          function (callback) {
            self.remote
              .once('ledger_closed', function (mesage) {
                  // console.log("LEDGER_CLOSED: A: %d: %s", ledger_index, ledger_hash);
                  callback();
                })
              .ledger_accept();
          },
          function (callback) {
            self.remote
              .once('ledger_closed', function (mesage) {
                  // console.log("LEDGER_CLOSED: B: %d: %s", ledger_index, ledger_hash);
                  callback();
                })
              .ledger_accept();
          },
        ], function (error) {
          // console.log("result: error=%s", error);
          buster.refute(error);
          if (error) done();
        });
    },

  "offer cancel past and future sequence" :
    function (done) {
      var self = this;
      var final_create;

      async.waterfall([
          function (callback) {
            self.remote.transaction()
              .payment('root', 'alice', Amount.from_json("10000.0"))
              .on('proposed', function (m) {
                  // console.log("PROPOSED: CreateAccount: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS', m);
                })
              .on('error', function(m) {
                  // console.log("error: %s", m);

                  buster.assert(false);
                  callback(m);
                })
              .submit();
          },
          // Past sequence but wrong
          function (m, callback) {
            self.remote.transaction()
              .offer_cancel("root", m.tx_json.Sequence)
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_cancel past: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS', m);
                })
              .submit();
          },
          // Same sequence
          function (m, callback) {
            self.remote.transaction()
              .offer_cancel("root", m.tx_json.Sequence+1)
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_cancel same: %s", JSON.stringify(m));
                  callback(m.result !== 'temBAD_SEQUENCE', m);
                })
              .submit();
          },
          // Future sequence
          function (m, callback) {
            // After a malformed transaction, need to recover correct sequence.
            self.remote.set_account_seq("root", self.remote.account_seq("root")-1);

            self.remote.transaction()
              .offer_cancel("root", m.tx_json.Sequence+2)
              .on('proposed', function (m) {
                  // console.log("ERROR: offer_cancel future: %s", JSON.stringify(m));
                  callback(m.result !== 'temBAD_SEQUENCE');
                })
              .submit();
          },
          // See if ledger_accept will crash.
          function (callback) {
            self.remote
              .once('ledger_closed', function (mesage) {
                  // console.log("LEDGER_CLOSED: A: %d: %s", ledger_index, ledger_hash);
                  callback();
                })
              .ledger_accept();
          },
          function (callback) {
            self.remote
              .once('ledger_closed', function (mesage) {
                  // console.log("LEDGER_CLOSED: B: %d: %s", ledger_index, ledger_hash);
                  callback();
                })
              .ledger_accept();
          },
          function (callback) {
            callback();
          }
        ], function (error) {
          // console.log("result: error=%s", error);
          buster.refute(error);

          done();
        });
    },

  "ripple currency conversion : entire offer" :
    // mtgox in, XRP out
    function (done) {
      var self = this;
      var seq;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Owner count 0.";

            testutils.verify_owner_count(self.remote, "bob", 0, callback);
          },
          function (callback) {
            self.what = "Set limits.";

            testutils.credit_limits(self.remote,
              {
                "alice" : "100/USD/mtgox",
                "bob" : "1000/USD/mtgox"
              },
              callback);
          },
          function (callback) {
            self.what = "Owner counts after trust.";

            testutils.verify_owner_counts(self.remote,
              {
                "alice" : 1,
                "bob" : 1,
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments(self.remote,
              {
                "mtgox" : "100/USD/alice"
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer.";

            self.remote.transaction()
              .offer_create("bob", "100/USD/mtgox", "500")
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');

                  seq = m.tx_json.Sequence;
                })
              .submit();
          },
          function (callback) {
            self.what = "Owner counts after offer create.";

            testutils.verify_owner_counts(self.remote,
              {
                "alice" : 1,
                "bob" : 2,
              },
              callback);
          },
          function (callback) {
            self.what = "Verify offer balance.";

            testutils.verify_offer(self.remote, "bob", seq, "100/USD/mtgox", "500", callback);
          },
          function (callback) {
            self.what = "Alice converts USD to XRP.";

            self.remote.transaction()
              .payment("alice", "alice", "500")
              .send_max("100/USD/mtgox")
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
                "alice"   : [ "0/USD/mtgox", String(10000000000+500-2*(Transaction.fees['default'].to_number())) ],
                "bob"     : "100/USD/mtgox",
              },
              callback);
          },
          function (callback) {
            self.what = "Verify offer consumed.";

            testutils.verify_offer_not_found(self.remote, "bob", seq, callback);
          },
          function (callback) {
            self.what = "Owner counts after consumed.";

            testutils.verify_owner_counts(self.remote,
              {
                "alice" : 1,
                "bob" : 1,
              },
              callback);
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

  "ripple currency conversion : offerer into debt" :
    // alice in, carol out
    function (done) {
      var self = this;
      var seq;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "carol"], callback);
          },
          function (callback) {
            self.what = "Set limits.";

            testutils.credit_limits(self.remote,
              {
                "alice" : "2000/EUR/carol",
                "bob" : "100/USD/alice",
                "carol" : "1000/EUR/bob"
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer to exchange.";

            self.remote.transaction()
              .offer_create("bob", "50/USD/alice", "200/EUR/carol")
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.result !== 'tecUNFUNDED_OFFER');

                  seq = m.tx_json.Sequence;
                })
              .submit();
          },
//          function (callback) {
//            self.what = "Alice converts USD to EUR via offer.";
//
//            self.remote.transaction()
//              .offer_create("alice", "200/EUR/carol", "50/USD/alice")
//              .on('proposed', function (m) {
//                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
//                  callback(m.result !== 'tesSUCCESS');
//
//                  seq = m.tx_json.Sequence;
//                })
//              .submit();
//          },
//          function (callback) {
//            self.what = "Verify balances.";
//
//            testutils.verify_balances(self.remote,
//              {
//                "alice"   : [ "-50/USD/bob", "200/EUR/carol" ],
//                "bob"     : [ "50/USD/alice", "-200/EUR/carol" ],
//                "carol"   : [ "-200/EUR/alice", "200/EUR/bob" ],
//              },
//              callback);
//          },
//          function (callback) {
//            self.what = "Verify offer consumed.";
//
//            testutils.verify_offer_not_found(self.remote, "bob", seq, callback);
//          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

  "ripple currency conversion : in parts" :
    function (done) {
      var self = this;
      var seq;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set limits.";

            testutils.credit_limits(self.remote,
              {
                "alice" : "200/USD/mtgox",
                "bob" : "1000/USD/mtgox"
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments(self.remote,
              {
                "mtgox" : "200/USD/alice"
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer.";

            self.remote.transaction()
              .offer_create("bob", "100/USD/mtgox", "500")
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');

                  seq = m.tx_json.Sequence;
                })
              .submit();
          },
          function (callback) {
            self.what = "Alice converts USD to XRP.";

            self.remote.transaction()
              .payment("alice", "alice", "200")
              .send_max("100/USD/mtgox")
              .on('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify offer balance.";

            testutils.verify_offer(self.remote, "bob", seq, "60/USD/mtgox", "300", callback);
          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances(self.remote,
              {
                "alice"   : [ "160/USD/mtgox", String(10000000000+200-2*(Transaction.fees['default'].to_number())) ],
                "bob"     : "40/USD/mtgox",
              },
              callback);
          },
          function (callback) {
            self.what = "Alice converts USD to XRP should fail due to PartialPayment.";

            self.remote.transaction()
              .payment("alice", "alice", "600")
              .send_max("100/USD/mtgox")
              .on('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  callback(m.result !== 'tecPATH_PARTIAL');
                })
              .submit();
          },
          function (callback) {
            self.what = "Alice converts USD to XRP.";

            self.remote.transaction()
              .payment("alice", "alice", "600")
              .send_max("100/USD/mtgox")
              .set_flags('PartialPayment')
              .on('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  callback(m.result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify offer consumed.";

            testutils.verify_offer_not_found(self.remote, "bob", seq, callback);
          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances(self.remote,
              {
                "alice"   : [ "100/USD/mtgox", String(10000000000+200+300-4*(Transaction.fees['default'].to_number())) ],
                "bob"     : "100/USD/mtgox",
              },
              callback);
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },
});

buster.testCase("Offer cross currency", {
  'setUp' : testutils.build_setup(),
  'tearDown' : testutils.build_teardown(),

  "ripple cross currency payment - start with XRP" :
    // alice --> [XRP --> carol --> USD/mtgox] --> bob

    function (done) {
      var self = this;
      var seq;

      // self.remote.set_trace();

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "carol", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set limits.";

            testutils.credit_limits(self.remote,
              {
                "carol" : "1000/USD/mtgox",
                "bob" : "2000/USD/mtgox"
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments(self.remote,
              {
                "mtgox" : "500/USD/carol"
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer.";

            self.remote.transaction()
              .offer_create("carol", "500", "50/USD/mtgox")
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');

                  seq = m.tx_json.Sequence;
                })
              .submit();
          },
          function (callback) {
            self.what = "Alice send USD/mtgox converting from XRP.";

            self.remote.transaction()
              .payment("alice", "bob", "25/USD/mtgox")
              .send_max("333")
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
//              "alice"   : [ "500" ],
                "bob"     : "25/USD/mtgox",
                "carol"   : "475/USD/mtgox",
              },
              callback);
          },
          function (callback) {
            self.what = "Verify offer consumed.";

            testutils.verify_offer_not_found(self.remote, "bob", seq, callback);
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

  "ripple cross currency payment - end with XRP" :
    // alice --> [USD/mtgox --> carol --> XRP] --> bob

    function (done) {
      var self = this;
      var seq;

      // self.remote.set_trace();

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "carol", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set limits.";

            testutils.credit_limits(self.remote,
              {
                "alice" : "1000/USD/mtgox",
                "carol" : "2000/USD/mtgox"
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments(self.remote,
              {
                "mtgox" : "500/USD/alice"
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer.";

            self.remote.transaction()
              .offer_create("carol", "50/USD/mtgox", "500")
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');

                  seq = m.tx_json.Sequence;
                })
              .submit();
          },
          function (callback) {
            self.what = "Alice send XRP to bob converting from USD/mtgox.";

            self.remote.transaction()
              .payment("alice", "bob", "250")
              .send_max("333/USD/mtgox")
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
                "alice"   : "475/USD/mtgox",
                "bob"     : "10000000250",
                "carol"   : "25/USD/mtgox",
              },
              callback);
          },
          function (callback) {
            self.what = "Verify offer partially consumed.";

            testutils.verify_offer(self.remote, "carol", seq, "25/USD/mtgox", "250", callback);
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

  "ripple cross currency bridged payment" :
    // alice --> [USD/mtgox --> carol --> XRP] --> [XRP --> dan --> EUR/bitstamp] --> bob

    function (done) {
      var self = this;
      var seq_carol;
      var seq_dan;

      // self.remote.set_trace();

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "carol", "dan", "bitstamp", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set limits.";

            testutils.credit_limits(self.remote,
              {
                "alice" : "1000/USD/mtgox",
                "bob" : "1000/EUR/bitstamp",
                "carol" : "1000/USD/mtgox",
                "dan" : "1000/EUR/bitstamp"
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments(self.remote,
              {
                "bitstamp" : "400/EUR/dan",
                "mtgox" : "500/USD/alice",
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer carol.";

            self.remote.transaction()
              .offer_create("carol", "50/USD/mtgox", "500")
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');

                  seq_carol = m.tx_json.Sequence;
                })
              .submit();
          },
          function (callback) {
            self.what = "Create offer dan.";

            self.remote.transaction()
              .offer_create("dan", "500", "50/EUR/bitstamp")
              .on('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.result !== 'tesSUCCESS');

                  seq_dan = m.tx_json.Sequence;
                })
              .submit();
          },
          function (callback) {
            self.what = "Alice send EUR/bitstamp to bob converting from USD/mtgox.";

            self.remote.transaction()
              .payment("alice", "bob", "30/EUR/bitstamp")
              .send_max("333/USD/mtgox")
              .path_add( [ { currency: "XRP" } ])
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
                "alice"   : "470/USD/mtgox",
                "bob"     : "30/EUR/bitstamp",
                "carol"   : "30/USD/mtgox",
                "dan"     : "370/EUR/bitstamp",
              },
              callback);
          },
          function (callback) {
            self.what = "Verify carol offer partially consumed.";

            testutils.verify_offer(self.remote, "carol", seq_carol, "20/USD/mtgox", "200", callback);
          },
          function (callback) {
            self.what = "Verify dan offer partially consumed.";

            testutils.verify_offer(self.remote, "dan", seq_dan, "200", "20/EUR/mtgox", callback);
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },
});

buster.testCase("Offer tests 3", {
  'setUp'     : testutils.build_setup(),
  // 'setUp'     : testutils.build_setup({ verbose: true }),
  // 'setUp'     : testutils.build_setup({ verbose: true, standalone: true }),
  'tearDown'  : testutils.build_teardown(),

  "offer create then cross offer" :
    function (done) {
      var self = this;
      var final_create;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["alice", "bob", "mtgox"], callback);
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
            self.what = "Set limits.";

            testutils.credit_limits(self.remote,
              {
                "alice" : "1000/USD/mtgox",
                "bob" : "1000/USD/mtgox",
                "mtgox" : "50/USD/alice",
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments(self.remote,
              {
                "mtgox" : [ "1/USD/bob" ],
                "alice" : [ "50/USD/mtgox" ]
              },
              callback);
          },
          function (callback) {
            self.what = "Set limits 2.";

            testutils.credit_limits(self.remote,
              {
                "mtgox" : "0/USD/alice",
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer alice.";

            self.remote.transaction()
              .offer_create("alice", "50/USD/mtgox", "150000.0")
              .on('proposed', function (m) {
                  // console.log("proposed: offer_create: %s", json.stringify(m));
                  callback(m.result !== 'tesSUCCESS');

                  seq_carol = m.tx_json.sequence;
                })
              .submit();
          },
          function (callback) {
            self.what = "Create offer bob.";

            self.remote.transaction()
              .offer_create("bob", "100.0", ".1/USD/mtgox")
              .on('proposed', function (m) {
                  // console.log("proposed: offer_create: %s", json.stringify(m));
                  callback(m.result !== 'tesSUCCESS');

                  seq_carol = m.tx_json.sequence;
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

            testutils.verify_balances(self.remote,
              {
                "alice"   : "-49.96666666666667/USD/mtgox",
                "bob"     : "0.9665/USD/mtgox",
              },
              callback);
          },
        ], function (error) {
          // console.log("result: error=%s", error);
          buster.refute(error);

          done();
        });
    },
});
// vim:sw=2:sts=2:ts=8:et
