var async       = require("async");
var assert      = require('assert');
var Amount      = require("ripple-lib").Amount;
var Remote      = require("ripple-lib").Remote;
var Transaction = require("ripple-lib").Transaction;
var Server      = require("./server").Server;
var testutils   = require("./testutils");
var config      = testutils.init_config();

suite("Offer tests", function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test("offer create then cancel in one ledger", function (done) {
      var self = this;
      var final_create;

      async.waterfall([
          function (callback) {
            $.remote.transaction()
              .offer_create("root", "500", "100/USD/root")
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS', m);
                })
              .on('final', function (m) {
                  // console.log("FINAL: offer_create: %s", JSON.stringify(m));

                  assert.strictEqual('tesSUCCESS', m.metadata.TransactionResult);

                  assert(final_create);
                })
              .submit();
          },
          function (m, callback) {
            $.remote.transaction()
              .offer_cancel("root", m.tx_json.Sequence)
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_cancel: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS', m);
                })
              .on('final', function (m) {
                  // console.log("FINAL: offer_cancel: %s", JSON.stringify(m, undefined, 2));

                  assert.strictEqual('tesSUCCESS', m.metadata.TransactionResult);
                  assert(final_create);
                  done();
                })
              .submit();
          },
          function (m, callback) {
            $.remote
              .once('ledger_closed', function (message) {
                  // console.log("LEDGER_CLOSED: %d: %s", ledger_index, ledger_hash);
                  final_create  = message;
                })
              .ledger_accept();
          }
        ], function (error) {
          // console.log("result: error=%s", error);
          assert(!error, self.what || "Unspecifide Error");

          done();
        });
  });

  test("offer create then offer create with cancel in one ledger", function (done) {
      var self = this;
      var final_create;
      var sequence_first;
      var sequence_second;
      var dones = 0;

      async.waterfall([
          function (callback) {
            $.remote.transaction()
              .offer_create("root", "500", "100/USD/root")
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS', m);
                })
              .on('final', function (m) {
                  // console.log("FINAL: offer_create: %s", JSON.stringify(m));

                  assert.strictEqual('tesSUCCESS', m.metadata.TransactionResult);
                  assert(final_create);

                  if (3 === ++dones)
                    done();
                })
              .submit();
          },
          function (m, callback) {
            sequence_first  = m.tx_json.Sequence;

            // Test canceling existant offer.
            $.remote.transaction()
              .offer_create("root", "300", "100/USD/root", undefined, sequence_first)
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS', m);
                })
              .on('final', function (m) {
                  // console.log("FINAL: offer_create: %s", JSON.stringify(m));

                  assert.strictEqual('tesSUCCESS', m.metadata.TransactionResult);
                  assert(final_create);

                  if (3 === ++dones)
                    done();
                })
              .submit();
          },
          function (m, callback) {
            sequence_second  = m.tx_json.Sequence;
            self.what = "Verify offer canceled.";

            testutils.verify_offer_not_found($.remote, "root", sequence_first, callback);
          },
          function (callback) {
            self.what = "Verify offer replaced.";

            testutils.verify_offer($.remote, "root", sequence_second, "300", "100/USD/root", callback);
          },
          function (callback) {
            // Test canceling non-existant offer.
            $.remote.transaction()
              .offer_create("root", "400", "200/USD/root", undefined, sequence_first)
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS', m);
                })
              .on('final', function (m) {
                  // console.log("FINAL: offer_create: %s", JSON.stringify(m));

                  assert.strictEqual('tesSUCCESS', m.metadata.TransactionResult);
                  assert(final_create);

                  if (3 === ++dones)
                    done();
                })
              .submit();
          },
          function (callback) {
            $.remote
              .once('ledger_closed', function (message) {
                  // console.log("LEDGER_CLOSED: %d: %s", ledger_index, ledger_hash);
                  final_create  = message;
                })
              .ledger_accept();
          }
        ], function (error) {
          // console.log("result: error=%s", error);
          assert(!error, self.what);

          done();
        });
    });

  test("offer create then self crossing offer, no trust lines with self", function (done) {
      var self = this;

      async.waterfall([
        function (callback) {
          self.what = "Create first offer.";

          $.remote.transaction()
            .offer_create("root", "500/BTC/root", "100/USD/root")
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
        },
        function (callback) {
          self.what = "Create crossing offer.";

          $.remote.transaction()
            .offer_create("root", "100/USD/root", "500/BTC/root")
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
        }
      ], function (error) {
        // console.log("result: error=%s", error);
        assert(!error, self.what);
        done();
      });
    });

  test("offer create then crossing offer with XRP. Negative balance.", function (done) {
      var self = this;

      var alices_initial_balance = 499946999680;
      var bobs_initial_balance = 10199999920;

      async.waterfall([
        function (callback) {
          self.what = "Create mtgox account.";
          testutils.payment($.remote, "root", "mtgox", 1149999730, callback);
        },
        function (callback) {
          self.what = "Create alice account.";

          testutils.payment($.remote, "root", "alice", alices_initial_balance, callback);
        },
        function (callback) {
          self.what = "Create bob account.";

          testutils.payment($.remote, "root", "bob", bobs_initial_balance, callback);
        },
        function (callback) {
          self.what = "Set transfer rate.";

          $.remote.transaction()
            .account_set("mtgox")
            .transfer_rate(1005000000)
            .once('proposed', function (m) {
                // console.log("proposed: %s", JSON.stringify(m));
                callback(m.engine_result !== 'tesSUCCESS');
              })
            .submit();
        },
        function (callback) {
          self.what = "Set limits.";

          testutils.credit_limits($.remote,
            {
              "alice" : "500/USD/mtgox",
              "bob" : "50/USD/mtgox",
              "mtgox" : "100/USD/alice",
            },
            callback);
        },
        function (callback) {
          self.what = "Distribute funds.";

          testutils.payments($.remote,
            {
              "mtgox" : [ "50/USD/alice", "2710505431213761e-33/USD/bob" ]
            },
            callback);
        },
        function (callback) {
          self.what = "Create first offer.";

          $.remote.transaction()
            .offer_create("alice", "50/USD/mtgox", "150000.0")    // get 50/USD pay 150000/XRP
              .once('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
        },
        function (callback) {
          self.what = "Unfund offer.";

          testutils.payments($.remote,
            {
              "alice" : "100/USD/mtgox"
            },
            callback);
        },
        function (callback) {
          self.what = "Set limits 2.";

          testutils.credit_limits($.remote,
            {
              "mtgox" : "0/USD/alice",
            },
            callback);
        },
        function (callback) {
          self.what = "Verify balances. 1";

          testutils.verify_balances($.remote,
            {
              "alice"   : [ "-50/USD/mtgox" ],
              "bob"     : [ "2710505431213761e-33/USD/mtgox" ],
            },
            callback);
        },
        function (callback) {
          self.what = "Create crossing offer.";

          $.remote.transaction()
            .offer_create("bob", "2000.0", "1/USD/mtgox")  // get 2,000/XRP pay 1/USD (has insufficient USD)
              .once('proposed', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
        },
        function (callback) {
          self.what = "Verify balances. 2";

          var alices_fees, alices_num_transactions, alices_tx_fee_units_total,
              alices_tx_fee_units_total, alices_final_balance,
              alices_tx_fees_total,

              bobs_fees, bobs_num_transactions, bobs_tx_fee_units_total,
                            bobs_tx_fee_units_total, bobs_final_balance,
                            bobs_tx_fees_total;

          alices_num_transactions = 3;
          alices_tx_fee_units_total = alices_num_transactions * Transaction.fee_units["default"]
          alices_tx_fees_total = $.remote.fee_tx(alices_tx_fee_units_total);
          alices_final_balance = Amount.from_json(alices_initial_balance)
                                       .subtract(alices_tx_fees_total);

          bobs_num_transactions = 2;
          bobs_tx_fee_units_total = bobs_num_transactions * Transaction.fee_units["default"]
          bobs_tx_fees_total = $.remote.fee_tx(bobs_tx_fee_units_total);
          bobs_final_balance = Amount.from_json(bobs_initial_balance)
                                       .subtract(bobs_tx_fees_total);

          testutils.verify_balances($.remote,
            {
              "alice"   : [ "-50/USD/mtgox", alices_final_balance.to_json()],
              "bob"     : [   "2710505431213761e-33/USD/mtgox",
              bobs_final_balance.to_json()

                  // bobs_final_balance.to_json()
                  // String(10199999920-($.remote.fee_tx(2*(Transaction.fee_units['default'])))).to_number()
                  ],
            },
            callback);
        },
//        function (callback) {
//          self.what = "Display ledger";
//
//          $.remote.request_ledger('current', true)
//            .on('success', function (m) {
//                console.log("Ledger: %s", JSON.stringify(m, undefined, 2));
//
//                callback();
//              })
//            .request();
//        },
      ], function (error) {
        // console.log("result: error=%s", error);
        assert(!error, self.what);
        done();
      });
    });

  test("offer create then crossing offer with XRP. Reverse order." , function (done) {
      var self = this;

      async.waterfall([
        function (callback) {
          self.what = "Create accounts.";

          testutils.create_accounts($.remote, "root", "100000.0", ["alice", "bob", "mtgox"], callback);
        },
        function (callback) {
          self.what = "Set limits.";

          testutils.credit_limits($.remote,
            {
              "alice" : "1000/USD/mtgox",
              "bob" : "1000/USD/mtgox"
            },
            callback);
        },
        function (callback) {
          self.what = "Distribute funds.";

          testutils.payments($.remote,
            {
              "mtgox" : "500/USD/alice"
            },
            callback);
        },
        function (callback) {
          self.what = "Create first offer.";

          $.remote.transaction()
            .offer_create("bob", "1/USD/mtgox", "4000.0")         // get 1/USD pay 4000/XRP : offer pays 4000 XRP for 1 USD
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
        },
        function (callback) {
          self.what = "Create crossing offer.";

          // Existing offer pays better than this wants.
          // Fully consume existing offer.
          // Pay 1 USD, get 4000 XRP.

          $.remote.transaction()
            .offer_create("alice", "150000.0", "50/USD/mtgox")  // get 150,000/XRP pay 50/USD : offer pays 1 USD for 3000 XRP
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
        },
        function (callback) {
          self.what = "Verify balances.";

          testutils.verify_balances($.remote,
            {
              // "bob"     : [   "1/USD/mtgox", String(100000000000-4000000000-(Number($.remote.fee_tx(Transaction.fee_units['default'] * 2).to_json())))  ],
              "bob"     : [   "1/USD/mtgox", String(100000000000-4000000000-($.remote.fee_tx(Transaction.fee_units['default'] * 2).to_number())) ],
              "alice"   : [ "499/USD/mtgox", String(100000000000+4000000000-($.remote.fee_tx(Transaction.fee_units['default'] * 2).to_number())) ],
            },
            callback);
        },
//        function (callback) {
//          self.what = "Display ledger";
//
//          $.remote.request_ledger('current', true)
//            .on('success', function (m) {
//                console.log("Ledger: %s", JSON.stringify(m, undefined, 2));
//
//                callback();
//              })
//            .request();
//        },
      ], function (error) {
        // console.log("result: error=%s", error);
        assert(!error, self.what);
        done();
      });
    });

  test("offer create then crossing offer with XRP.", function (done) {
      var self = this;

      async.waterfall([
        function (callback) {
          self.what = "Create accounts.";

          testutils.create_accounts($.remote, "root", "100000.0", ["alice", "bob", "mtgox"], callback);
        },
        function (callback) {
          self.what = "Set limits.";

          testutils.credit_limits($.remote,
            {
              "alice" : "1000/USD/mtgox",
              "bob" : "1000/USD/mtgox"
            },
            callback);
        },
        function (callback) {
          self.what = "Distribute funds.";

          testutils.payments($.remote,
            {
              "mtgox" : "500/USD/alice"
            },
            callback);
        },
        function (callback) {
          self.what = "Create first offer.";

          $.remote.transaction()
            .offer_create("alice", "150000.0", "50/USD/mtgox")  // pays 1 USD for 3000 XRP
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
        },
        function (callback) {
          self.what = "Create crossing offer.";

          $.remote.transaction()
            .offer_create("bob", "1/USD/mtgox", "4000.0") // pays 4000 XRP for 1 USD
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
        },
        function (callback) {
          self.what = "Verify balances.";

          // New offer pays better than old wants.
          // Fully consume new offer.
          // Pay 1 USD, get 3000 XRP.

          testutils.verify_balances($.remote,
            {
              "alice"   : [ "499/USD/mtgox", String(100000000000+3000000000-($.remote.fee_tx(2*(Transaction.fee_units['default'])).to_number())) ],
              "bob"     : [   "1/USD/mtgox", String(100000000000-3000000000-($.remote.fee_tx(2*(Transaction.fee_units['default'])).to_number())) ],
            },
            callback);
        },
//        function (callback) {
//          self.what = "Display ledger";
//
//          $.remote.request_ledger('current', true)
//            .on('success', function (m) {
//                console.log("Ledger: %s", JSON.stringify(m, undefined, 2));
//
//                callback();
//              })
//            .request();
//        },
      ], function (error) {
        // console.log("result: error=%s", error);
        assert(!error, self.what);
        done();
      });
    });

  test("offer create then crossing offer with XRP with limit override.", function (done) {
      var self = this;

      async.waterfall([
        function (callback) {
          self.what = "Create accounts.";

          testutils.create_accounts($.remote, "root", "100000.0", ["alice", "bob", "mtgox"], callback);
        },
        function (callback) {
          self.what = "Set limits.";

          testutils.credit_limits($.remote,
            {
              "alice" : "1000/USD/mtgox",
//              "bob" : "1000/USD/mtgox"
            },
            callback);
        },
        function (callback) {
          self.what = "Distribute funds.";

          testutils.payments($.remote,
            {
              "mtgox" : "500/USD/alice"
            },
            callback);
        },
        function (callback) {
          self.what = "Create first offer.";

          $.remote.transaction()
            .offer_create("alice", "150000.0", "50/USD/mtgox")  // 300 XRP = 1 USD
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
        },
//        function (callback) {
//          self.what = "Display ledger";
//
//          $.remote.request_ledger('current', true)
//            .on('success', function (m) {
//                console.log("Ledger: %s", JSON.stringify(m, undefined, 2));
//
//                callback();
//              })
//            .request();
//        },
        function (callback) {
          self.what = "Create crossing offer.";

          $.remote.transaction()
            .offer_create("bob", "1/USD/mtgox", "3000.0") //
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
        },
        function (callback) {
          self.what = "Verify balances.";

          testutils.verify_balances($.remote,
            {
              "alice"   : [ "499/USD/mtgox", String(100000000000+3000000000-($.remote.fee_tx(2*(Transaction.fee_units['default'])).to_number())) ],
              "bob"     : [   "1/USD/mtgox", String(100000000000-3000000000-($.remote.fee_tx(1*(Transaction.fee_units['default'])).to_number())) ],
            },
            callback);
        },
//        function (callback) {
//          self.what = "Display ledger";
//
//          $.remote.request_ledger('current', true)
//            .on('success', function (m) {
//                console.log("Ledger: %s", JSON.stringify(m, undefined, 2));
//
//                callback();
//              })
//            .request();
//        },
      ], function (error) {
        // console.log("result: error=%s", error);
        assert(!error, self.what);
        done();
      });
    });

  test("offer_create then ledger_accept then offer_cancel then ledger_accept.", function (done) {
      var self = this;
      var final_create;
      var offer_seq;

      async.waterfall([
          function (callback) {
            $.remote.transaction()
              .offer_create("root", "500", "100/USD/root")
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  offer_seq = m.tx_json.Sequence;

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .on('final', function (m) {
                  // console.log("FINAL: offer_create: %s", JSON.stringify(m));

                  assert.strictEqual('tesSUCCESS', m.metadata.TransactionResult);

                  final_create  = m;

                  callback();
                })
              .submit();
          },
          function (callback) {
            if (!final_create) {
              $.remote
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

            $.remote.transaction()
              .offer_cancel("root", offer_seq)
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_cancel: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .on('final', function (m) {
                  // console.log("FINAL: offer_cancel: %s", JSON.stringify(m));

                  assert.strictEqual('tesSUCCESS', m.metadata.TransactionResult);
                  assert(final_create);

                  done();
                })
              .submit();
          },
          // See if ledger_accept will crash.
          function (callback) {
            $.remote
              .once('ledger_closed', function (mesage) {
                  // console.log("LEDGER_CLOSED: A: %d: %s", ledger_index, ledger_hash);
                  callback();
                })
              .ledger_accept();
          },
          function (callback) {
            $.remote
              .once('ledger_closed', function (mesage) {
                  // console.log("LEDGER_CLOSED: B: %d: %s", ledger_index, ledger_hash);
                  callback();
                })
              .ledger_accept();
          },
        ], function (error) {
          // console.log("result: error=%s", error);
          assert(!error, self.what);

          if (error) done();
        });
    });

  test.skip("new user offer_create then ledger_accept then offer_cancel then ledger_accept.", function (done) {

      var self = this;
      var final_create;
      var offer_seq;

      async.waterfall([
          function (callback) {
            $.remote.transaction()
              .payment('root', 'alice', "1000")
              .on('submitted', function (m) {
                // console.log("proposed: %s", JSON.stringify(m));
                assert.strictEqual(m.engine_result, 'tesSUCCESS');
                callback();
              })
              .submit()
          },
          function (callback) {
            $.remote.transaction()
              .offer_create("alice", "500", "100/USD/alice")
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

                  offer_seq = m.tx_json.Sequence;

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .on('final', function (m) {
                  // console.log("FINAL: offer_create: %s", JSON.stringify(m));

                  assert.strictEqual('tesSUCCESS', m.metadata.TransactionResult);

                  final_create  = m;

                  callback();
                })
              .submit();
          },
          function (callback) {
            if (!final_create) {
              $.remote
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

            $.remote.transaction()
              .offer_cancel("alice", offer_seq)
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_cancel: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .on('final', function (m) {
                  // console.log("FINAL: offer_cancel: %s", JSON.stringify(m));

                  assert.strictEqual('tesSUCCESS', m.metadata.TransactionResult);
                  assert(final_create);

                  done();
                })
              .submit();
          },
          // See if ledger_accept will crash.
          function (callback) {
            $.remote
              .once('ledger_closed', function (mesage) {
                  // console.log("LEDGER_CLOSED: A: %d: %s", ledger_index, ledger_hash);
                  callback();
                })
              .ledger_accept();
          },
          function (callback) {
            $.remote
              .once('ledger_closed', function (mesage) {
                  // console.log("LEDGER_CLOSED: B: %d: %s", ledger_index, ledger_hash);
                  callback();
                })
              .ledger_accept();
          },
        ], function (error) {
          // console.log("result: error=%s", error);
          assert(!error, self.what);
          if (error) done();
        });
    });

  test("offer cancel past and future sequence", function (done) {
      var self = this;
      var final_create;

      async.waterfall([
          function (callback) {
            $.remote.transaction()
              .payment('root', 'alice', Amount.from_json("10000.0"))
              .once('submitted', function (m) {
                  //console.log("PROPOSED: CreateAccount: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS', m);
                })
              .once('error', function(m) {
                  //console.log("error: %s", m);
                  assert(false);
                  callback(m);
                })
              .submit();
          },
          // Past sequence but wrong
          function (m, callback) {
            $.remote.transaction()
              .offer_cancel("root", m.tx_json.Sequence)
              .once('submitted', function (m) {
                  //console.log("PROPOSED: offer_cancel past: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS', m);
                })
              .submit();
          },
          // Same sequence
          function (m, callback) {
            $.remote.transaction()
              .offer_cancel("root", m.tx_json.Sequence+1)
              .once('submitted', function (m) {
                  //console.log("PROPOSED: offer_cancel same: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'temBAD_SEQUENCE', m);
                })
              .submit();
          },
          // Future sequence
          function (m, callback) {
            $.remote.transaction()
              .offer_cancel("root", m.tx_json.Sequence+2)
              .once('submitted', function (m) {
                  //console.log("ERROR: offer_cancel future: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'temBAD_SEQUENCE');
                })
              .submit();
          },
          // See if ledger_accept will crash.
          function (callback) {
            $.remote
              .once('ledger_closed', function (message) {
                  //console.log("LEDGER_CLOSED: A: %d: %s", message.ledger_index, message.ledger_hash);
                  callback();
                })
              .ledger_accept();
          },
          function (callback) {
            $.remote
              .once('ledger_closed', function (mesage) {
                  //console.log("LEDGER_CLOSED: B: %d: %s", message.ledger_index, message.ledger_hash);
                  callback();
                })
              .ledger_accept();
          },
          function (callback) {
            callback();
          }
        ], function (error) {
          //console.log("result: error=%s", error);
          assert(!error, self.what);
          done();
        });
    });

  test("ripple currency conversion : entire offer", function (done) {
    // mtgox in, XRP out
      var self = this;
      var seq;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts($.remote, "root", "10000.0", ["alice", "bob", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Owner count 0.";

            testutils.verify_owner_count($.remote, "bob", 0, callback);
          },
          function (callback) {
            self.what = "Set limits.";

            testutils.credit_limits($.remote,
              {
                "alice" : "100/USD/mtgox",
                "bob" : "1000/USD/mtgox"
              },
              callback);
          },
          function (callback) {
            self.what = "Owner counts after trust.";

            testutils.verify_owner_counts($.remote,
              {
                "alice" : 1,
                "bob" : 1,
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments($.remote,
              {
                "mtgox" : "100/USD/alice"
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer.";

            $.remote.transaction()
              .offer_create("bob", "100/USD/mtgox", "500")
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS');

                  seq = m.tx_json.Sequence;
                })
              .submit();
          },
          function (callback) {
            self.what = "Owner counts after offer create.";

            testutils.verify_owner_counts($.remote,
              {
                "alice" : 1,
                "bob" : 2,
              },
              callback);
          },
          function (callback) {
            self.what = "Verify offer balance.";

            testutils.verify_offer($.remote, "bob", seq, "100/USD/mtgox", "500", callback);
          },
          function (callback) {
            self.what = "Alice converts USD to XRP.";

            $.remote.transaction()
              .payment("alice", "alice", "500")
              .send_max("100/USD/mtgox")
              .on('submitted', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances($.remote,
              {
                "alice"   : [ "0/USD/mtgox", String(10000000000+500-($.remote.fee_tx(2*(Transaction.fee_units['default'])).to_number())) ],
                "bob"     : "100/USD/mtgox",
              },
              callback);
          },
          function (callback) {
            self.what = "Verify offer consumed.";

            testutils.verify_offer_not_found($.remote, "bob", seq, callback);
          },
          function (callback) {
            self.what = "Owner counts after consumed.";

            testutils.verify_owner_counts($.remote,
              {
                "alice" : 1,
                "bob" : 1,
              },
              callback);
          },
        ], function (error) {
          assert(!error, self.what);
          done();
        });
    });

  test("ripple currency conversion : offerer into debt", function (done) {
    // alice in, carol out
      var self = this;
      var seq;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts($.remote, "root", "10000.0", ["alice", "bob", "carol"], callback);
          },
          function (callback) {
            self.what = "Set limits.";

            testutils.credit_limits($.remote,
              {
                "alice" : "2000/EUR/carol",
                "bob" : "100/USD/alice",
                "carol" : "1000/EUR/bob"
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer to exchange.";

            $.remote.transaction()
              .offer_create("bob", "50/USD/alice", "200/EUR/carol")
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'tecUNFUNDED_OFFER');

                  seq = m.tx_json.Sequence;
                })
              .submit();
          },
//          function (callback) {
//            self.what = "Alice converts USD to EUR via offer.";
//
//            $.remote.transaction()
//              .offer_create("alice", "200/EUR/carol", "50/USD/alice")
//              .on('submitted', function (m) {
//                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
//                  callback(m.engine_result !== 'tesSUCCESS');
//
//                  seq = m.tx_json.Sequence;
//                })
//              .submit();
//          },
//          function (callback) {
//            self.what = "Verify balances.";
//
//            testutils.verify_balances($.remote,
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
//            testutils.verify_offer_not_found($.remote, "bob", seq, callback);
//          },
        ], function (error) {
          assert(!error, self.what);
          done();
        });
    });

  test("ripple currency conversion : in parts", function (done) {
      var self = this;
      var seq;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts($.remote, "root", "10000.0", ["alice", "bob", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set limits.";

            testutils.credit_limits($.remote,
              {
                "alice" : "200/USD/mtgox",
                "bob" : "1000/USD/mtgox"
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments($.remote,
              {
                "mtgox" : "200/USD/alice"
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer.";

            $.remote.transaction()
              .offer_create("bob", "100/USD/mtgox", "500")
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS');

                  seq = m.tx_json.Sequence;
                })
              .submit();
          },
          function (callback) {
            self.what = "Alice converts USD to XRP.";

            $.remote.transaction()
              .payment("alice", "alice", "200")
              .send_max("100/USD/mtgox")
              .on('submitted', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify offer balance.";

            testutils.verify_offer($.remote, "bob", seq, "60/USD/mtgox", "300", callback);
          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances($.remote,
              {
                "alice"   : [ "160/USD/mtgox", String(10000000000+200-($.remote.fee_tx(2*(Transaction.fee_units['default'])).to_number())) ],
                "bob"     : "40/USD/mtgox",
              },
              callback);
          },
          function (callback) {
            self.what = "Alice converts USD to XRP should fail due to PartialPayment.";

            $.remote.transaction()
              .payment("alice", "alice", "600")
              .send_max("100/USD/mtgox")
              .on('submitted', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  callback(m.engine_result !== 'tecPATH_PARTIAL');
                })
              .submit();
          },
          function (callback) {
            self.what = "Alice converts USD to XRP.";

            $.remote.transaction()
              .payment("alice", "alice", "600")
              .send_max("100/USD/mtgox")
              .set_flags('PartialPayment')
              .on('submitted', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify offer consumed.";

            testutils.verify_offer_not_found($.remote, "bob", seq, callback);
          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances($.remote,
              {
                "alice"   : [ "100/USD/mtgox", String(10000000000+200+300-($.remote.fee_tx(4*(Transaction.fee_units['default'])).to_number())) ],
                "bob"     : "100/USD/mtgox",
              },
              callback);
          },
        ], function (error) {
          assert(!error, self.what);
          done();
        });
    });
});

suite("Offer cross currency", function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test("ripple cross currency payment - start with XRP", function (done) {
    // alice --> [XRP --> carol --> USD/mtgox] --> bob
      var self = this;
      var seq;

      // $.remote.set_trace();

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts($.remote, "root", "10000.0", ["alice", "bob", "carol", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set limits.";

            testutils.credit_limits($.remote,
              {
                "carol" : "1000/USD/mtgox",
                "bob" : "2000/USD/mtgox"
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments($.remote,
              {
                "mtgox" : "500/USD/carol"
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer.";

            $.remote.transaction()
              .offer_create("carol", "500.0", "50/USD/mtgox")
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS');

                  seq = m.tx_json.Sequence;
                })
              .submit();
          },
          function (callback) {
            self.what = "Alice send USD/mtgox converting from XRP.";

            $.remote.transaction()
              .payment("alice", "bob", "25/USD/mtgox")
              .send_max("333.0")
              .on('submitted', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances($.remote,
              {
//              "alice"   : [ "500" ],
                "bob"     : "25/USD/mtgox",
                "carol"   : "475/USD/mtgox",
              },
              callback);
          },
          function (callback) {
            self.what = "Verify offer consumed.";

            testutils.verify_offer_not_found($.remote, "bob", seq, callback);
          },
        ], function (error) {
          assert(!error, self.what);
          done();
        });
    });

  test("ripple cross currency payment - end with XRP", function (done) {
    // alice --> [USD/mtgox --> carol --> XRP] --> bob
      var self = this;
      var seq;

      // $.remote.set_trace();

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts($.remote, "root", "10000.0", ["alice", "bob", "carol", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set limits.";

            testutils.credit_limits($.remote,
              {
                "alice" : "1000/USD/mtgox",
                "carol" : "2000/USD/mtgox"
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments($.remote,
              {
                "mtgox" : "500/USD/alice"
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer.";

            $.remote.transaction()
              .offer_create("carol", "50/USD/mtgox", "500")
              .on('submitted', function (m) {
                  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS');

                  seq = m.tx_json.Sequence;
                })
              .submit();
          },
          function (callback) {
            self.what = "Alice send XRP to bob converting from USD/mtgox.";

            $.remote.transaction()
              .payment("alice", "bob", "250")
              .send_max("333/USD/mtgox")
              .on('submitted', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances($.remote,
              {
                "alice"   : "475/USD/mtgox",
                "bob"     : "10000000250",
                "carol"   : "25/USD/mtgox",
              },
              callback);
          },
          function (callback) {
            self.what = "Verify offer partially consumed.";

            testutils.verify_offer($.remote, "carol", seq, "25/USD/mtgox", "250", callback);
          },
        ], function (error) {
          assert(!error, self.what);
          done();
        });
    });

  test("ripple cross currency bridged payment", function (done) {
    // alice --> [USD/mtgox --> carol --> XRP] --> [XRP --> dan --> EUR/bitstamp] --> bob
      var self = this;
      var seq_carol;
      var seq_dan;

      //$.remote.set_trace();

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts($.remote, "root", "10000.0", ["alice", "bob", "carol", "dan", "bitstamp", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set limits.";

            testutils.credit_limits($.remote,
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

            testutils.payments($.remote,
              {
                "bitstamp" : "400/EUR/dan",
                "mtgox" : "500/USD/alice",
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer carol.";

            $.remote.transaction()
              .offer_create("carol", "50/USD/mtgox", "500")
              .once('proposed', function (m) {
                  //console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS');

                  seq_carol = m.tx_json.Sequence;
                })
              .submit();
          },
          function (callback) {
            self.what = "Create offer dan.";

            $.remote.transaction()
              .offer_create("dan", "500", "50/EUR/bitstamp")
              .once('proposed', function (m) {
                  //console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS');

                  seq_dan = m.tx_json.Sequence;
                })
              .submit();
          },
          function (callback) {
            self.what = "Alice send EUR/bitstamp to bob converting from USD/mtgox.";

            $.remote.transaction()
              .payment("alice", "bob", "30/EUR/bitstamp")
              .send_max("333/USD/mtgox")
              .pathAdd( [ { currency: "XRP" } ])
              .once('submitted', function (m) {
                  //console.log("PROPOSED: %s", JSON.stringify(m));

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances($.remote,
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

            testutils.verify_offer($.remote, "carol", seq_carol, "20/USD/mtgox", "200", callback);
          },
          function (callback) {
            self.what = "Verify dan offer partially consumed.";

            testutils.verify_offer($.remote, "dan", seq_dan, "200", "20/EUR/mtgox", callback);
          },
        ], function (error) {
          assert(!error, self.what);
          done();
        });
    });
});

suite("Offer tests 3", function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test("offer fee consumes funds", function (done) {
      // $.remote.trace=true
      var self = this;
      var final_create;
      var seq_carol;

      async.waterfall([
          function (callback) {
            // Provide micro amounts to compensate for fees to make results round nice.
            self.what = "Create accounts.";

            // Alice has 3 entries in the ledger, via trust lines
            var max_owner_count = 3; //
            // We start off with a
            var reserve_amount = $.remote.reserve(max_owner_count);
            // console.log('reserve', reserve_amount.to_json())
            // console.log("\n");
            // console.log("reserve_amount reserve(max_owner_count=%s): %s", max_owner_count,  reserve_amount.to_human());

            // this.tx_json.Fee = this.remote.fee_tx(this.fee_units()).to_json();

             //  1 for each trust limit == 3 (alice < mtgox/amazon/bitstamp)
             //  1 for payment          == 4
            var max_txs_per_user = 4;

            // We don't have access to the tx object[s] created below so we
            // just dig into fee_units straight away
            var fee_units_for_all_txs = ( Transaction.fee_units["default"] *
                                          max_txs_per_user );

            var fees = $.remote.fee_tx(fee_units_for_all_txs);
            var starting_xrp = reserve_amount.add(fees)
            // console.log("starting_xrp after %s fee units: ",  fee_units_for_all_txs, starting_xrp.to_human());
            starting_xrp = starting_xrp.add(Amount.from_json('100.0'));

            testutils.create_accounts($.remote,
                "root",
                starting_xrp.to_json(),
                ["alice", "bob", "mtgox", "amazon", "bitstamp"],
                callback);
          },
          function (callback) {
            self.what = "Set limits.";

            testutils.credit_limits($.remote,
              {
                "alice" : ["1000/USD/mtgox", "1000/USD/amazon","1000/USD/bitstamp"],
                "bob" :   ["1000/USD/mtgox", "1000/USD/amazon"],
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments($.remote,
              {
                "mtgox" : [ "500/USD/bob" ],
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer bob.";

            $.remote.transaction()
              .offer_create("bob", "200.0", "200/USD/mtgox")
              .on('submitted', function (m) {
                  // console.log("proposed: offer_create: %s", json.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS');
                  seq_carol = m.tx_json.sequence;
                })
              .submit();
          },
          function (callback) {
            // Alice has 350 fees - a reserve of 50 = 250 reserve = 100 available.
            // Ask for more than available to prove reserve works.
            self.what = "Create offer alice.";

            $.remote.transaction()
              .offer_create("alice", "200/USD/mtgox", "200.0")
              .on('submitted', function (m) {
                  // console.log("proposed: offer_create: %s", json.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS');

                  seq_carol = m.tx_json.sequence;
                })
              .submit();
          },
//          function (callback) {
//            self.what = "Display ledger";
//
//            $.remote.request_ledger('current', true)
//              .on('success', function (m) {
//                  console.log("Ledger: %s", JSON.stringify(m, undefined, 2));
//
//                  callback();
//                })
//              .request();
//          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances($.remote,
              {
                "alice"   : [ "100/USD/mtgox", "350.0"],
                "bob"     : ["400/USD/mtgox", ],
              },
              callback);
          },
        ], function (error) {
          // console.log("result: error=%s", error);
          assert(!error, self.what);

          done();
        });
    });

  test("offer create then cross offer", function (done) {
      var self = this;
      var final_create;
      var seq_carol;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts($.remote, "root", "10000.0", ["alice", "bob", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set transfer rate.";

            $.remote.transaction()
              .account_set("mtgox")
              .transfer_rate(1005000000)
              .once('proposed', function (m) {
                  // console.log("proposed: %s", JSON.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
          },
          function (callback) {
            self.what = "Set limits.";

            testutils.credit_limits($.remote,
              {
                "alice" : "1000/USD/mtgox",
                "bob" : "1000/USD/mtgox",
                "mtgox" : "50/USD/alice",
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments($.remote,
              {
                "mtgox" : [ "1/USD/bob" ],
                "alice" : [ "50/USD/mtgox" ]
              },
              callback);
          },
          function (callback) {
            self.what = "Set limits 2.";

            testutils.credit_limits($.remote,
              {
                "mtgox" : "0/USD/alice",
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer alice.";

            $.remote.transaction()
              .offer_create("alice", "50/USD/mtgox", "150000.0")
              .once('submitted', function (m) {
                  // console.log("proposed: offer_create: %s", json.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS');

                  seq_carol = m.tx_json.sequence;
                })
              .submit();
          },
          function (callback) {
            self.what = "Create offer bob.";

            $.remote.transaction()
              .offer_create("bob", "100.0", ".1/USD/mtgox")
              .once('submitted', function (m) {
                  // console.log("proposed: offer_create: %s", json.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS');

                  seq_carol = m.tx_json.sequence;
                })
              .submit();
          },
//          function (callback) {
//            self.what = "Display ledger";
//
//            $.remote.request_ledger('current', true)
//              .on('success', function (m) {
//                  console.log("Ledger: %s", JSON.stringify(m, undefined, 2));
//
//                  callback();
//                })
//              .request();
//          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances($.remote,
              {
                "alice"   : "-49.96666666666667/USD/mtgox",
                "bob"     : "0.9665/USD/mtgox",
              },
              callback);
          },
        ], function (error) {
          // console.log("result: error=%s", error);
          assert(!error, self.what);
          done();
        });
    });
});

suite("Offer tfSell", function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test("basic sell", function (done) {
      var self = this;
      var final_create, seq_carol;

      async.waterfall([
          function (callback) {
            // Provide micro amounts to compensate for fees to make results round nice.
            self.what = "Create accounts.";

            var req_amount = $.remote.reserve(1).add($.remote.fee_tx(20)).add(100000000);
            testutils.create_accounts($.remote, "root", req_amount.to_json(),
                                      ["alice", "bob", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set limits.";

            testutils.credit_limits($.remote,
              {
                "alice" : "1000/USD/mtgox",
                "bob" : "1000/USD/mtgox",
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments($.remote,
              {
                "mtgox" : [ "500/USD/bob" ],
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer bob.";

            $.remote.transaction()
              .offer_create("bob", "200.0", "200/USD/mtgox")
              .set_flags('Sell')            // Should not matter at all.
              .on('submitted', function (m) {
                  // console.log("proposed: offer_create: %s", json.stringify(m));
                  if (m.engine_result !== 'tesSUCCESS') {
                    throw new Error("Bob's OfferCreate tx did not succeed: "+m.engine_result);
                  } else callback(null);

                  seq_carol = m.tx_json.sequence;
                })
              .submit();
          },
          function (callback) {
            // Alice has 350 + fees - a reserve of 50 = 250 reserve = 100 available.
            // Ask for more than available to prove reserve works.
            self.what = "Create offer alice.";

            $.remote.transaction()
              .offer_create("alice", "200/USD/mtgox", "200.0")
              .set_flags('Sell')            // Should not matter at all.
              .on('submitted', function (m) {
                  // console.log("proposed: offer_create: %s", json.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS');

                  seq_carol = m.tx_json.sequence;
                })
              .submit();
          },
//          function (callback) {
//            self.what = "Display ledger";
//
//            $.remote.request_ledger('current', true)
//              .on('success', function (m) {
//                  console.log("Ledger: %s", JSON.stringify(m, undefined, 2));
//
//                  callback();
//                })
//              .request();
//          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances($.remote,
              {
                "alice"   : [ "100/USD/mtgox", "250.0" ],
                "bob"     : "400/USD/mtgox",
              },
              callback);
          },
        ], function (error) {
          // console.log("result: error=%s", error);
          assert(!error, self.what);

          done();
        });
    });

  test("2x sell exceed limit", function (done) {
      var self = this;
      var final_create, seq_carol;

      async.waterfall([
          function (callback) {
            // Provide micro amounts to compensate for fees to make results round nice.
            self.what = "Create accounts.";
            var starting_xrp = $.amount_for({
              ledger_entries: 1,
              default_transactions: 2,
              extra: '100.0'
            });
            testutils.create_accounts($.remote, "root", starting_xrp, ["alice", "bob", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set limits.";

            testutils.credit_limits($.remote,
              {
                "alice" : "150/USD/mtgox",
                "bob" : "1000/USD/mtgox",
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments($.remote,
              {
                "mtgox" : [ "500/USD/bob" ],
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer bob.";

            // Taker pays 200 XRP for 100 USD.
            // Selling USD.
            $.remote.transaction()
              .offer_create("bob", "100.0", "200/USD/mtgox")
              .on('submitted', function (m) {
                  // console.log("proposed: offer_create: %s", json.stringify(m));
                  callback(m.engine_result !== 'tesSUCCESS');
                  seq_carol = m.tx_json.sequence;
                })
              .submit();
          },
          function (callback) {
            // Alice has 350 fees - a reserve of 50 = 250 reserve = 100 available.
            // Ask for more than available to prove reserve works.
            self.what = "Create offer alice.";

            // Taker pays 100 USD for 100 XRP.
            // Selling XRP.
            // Will sell all 100 XRP and get more USD than asked for.
            $.remote.transaction()
              .offer_create("alice", "100/USD/mtgox", "100.0")
              .set_flags('Sell')
              .on('submitted', function (m) {
                  // console.log("proposed: offer_create: %s", json.stringify(m));
                  if (m.engine_result !== 'tesSUCCESS') {
                    callback(new Error("Alice's OfferCreate didn't succeed: "+m.engine_result));
                  } else callback(null);

                  seq_carol = m.tx_json.sequence;
                })
              .submit();
          },
//          function (callback) {
//            self.what = "Display ledger";
//
//            $.remote.request_ledger('current', true)
//              .on('success', function (m) {
//                  console.log("Ledger: %s", JSON.stringify(m, undefined, 2));
//
//                  callback();
//                })
//              .request();
//          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances($.remote,
              {
                "alice"   : [ "200/USD/mtgox", "250.0" ],
                "bob"     : "300/USD/mtgox",
              },
              callback);
          },
        ], function (error) {
          // console.log("result: error=%s", error);
          assert(!error, self.what);

          done();
        });
    });
});

suite("Client Issue #535", function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test("gateway cross currency", function (done) {
      var self = this;
      var final_create;
      var seq_carol;

      async.waterfall([
          function (callback) {
            // Provide micro amounts to compensate for fees to make results round nice.
            self.what = "Create accounts.";

            var starting_xrp = $.amount_for({
              ledger_entries: 1,
              default_transactions: 2,
              extra: '100.1'
            });

            testutils.create_accounts($.remote, "root", starting_xrp, ["alice", "bob", "mtgox"], callback);
          },
          function (callback) {
            self.what = "Set limits.";

            testutils.credit_limits($.remote,
              {
                "alice" : [ "1000/XTS/mtgox", "1000/XXX/mtgox" ],
                "bob" : [ "1000/XTS/mtgox", "1000/XXX/mtgox" ],
              },
              callback);
          },
          function (callback) {
            self.what = "Distribute funds.";

            testutils.payments($.remote,
              {
                "mtgox" : [ "100/XTS/alice", "100/XXX/alice", "100/XTS/bob", "100/XXX/bob", ],
              },
              callback);
          },
          function (callback) {
            self.what = "Create offer alice.";

            $.remote.transaction()
              .offer_create("alice", "100/XTS/mtgox", "100/XXX/mtgox")
              .on('submitted', function(m) {
                if (m.engine_result === 'tesSUCCESS') {
                  callback();
                } else {
                  // console.log("proposed: %s", JSON.stringify(m, undefined, 2));
                  callback(m);
                }

                seq_carol = m.tx_json.sequence;
              })
              .submit();
          },
          function (callback) {
            self.what = "Bob converts XTS to XXX.";

            $.remote.transaction()
              .payment("bob", "bob", "1/XXX/bob")
              .send_max("1.5/XTS/bob")
              .build_path(true)
              .on('submitted', function (m) {
                  if (m.engine_result !== 'tesSUCCESS')
                    console.log("proposed: %s", JSON.stringify(m, undefined, 2));

                  callback(m.engine_result !== 'tesSUCCESS');
                })
              .submit();
          },
//          function (callback) {
//            self.what = "Display ledger";
//
//            $.remote.request_ledger('current', true)
//              .on('success', function (m) {
//                  console.log("Ledger: %s", JSON.stringify(m, undefined, 2));
//
//                  callback();
//                })
//              .request();
//          },
          function (callback) {
            self.what = "Verify balances.";

            testutils.verify_balances($.remote,
              {
                "alice"   : [ "101/XTS/mtgox", "99/XXX/mtgox", ],
                "bob"   : [ "99/XTS/mtgox", "101/XXX/mtgox", ],
              },
              callback);
          },
      ], function (error) {
        if (error)
          //console.log("result: %s: error=%s", self.what, error);
        assert(!error, self.what);
        done();
      });
  });
});
// vim:sw=2:sts=2:ts=8:et
