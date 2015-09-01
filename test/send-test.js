var async     = require("async");
var assert    = require('assert');
var Amount    = require("ripple-lib").Amount;
var Remote    = require("ripple-lib").Remote;
var Server    = require("./server").Server;
var testutils = require("./testutils");
var config    = testutils.init_config();

suite('Sending', function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test("send XRP to non-existent account with insufficent fee", function (done) {
    var self    = this;
    var ledgers = 20;
    var got_proposed;

    $.remote.transaction()
    .payment('root', 'alice', "1")
    .once('submitted', function (m) {
      // Transaction got an error.
      // console.log("proposed: %s", JSON.stringify(m));
      assert.strictEqual(m.engine_result, 'tecNO_DST_INSUF_XRP');
      got_proposed  = true;
      $.remote.ledger_accept();    // Move it along.
    })
    .once('final', function (m) {
      // console.log("final: %s", JSON.stringify(m, undefined, 2));
      assert.strictEqual(m.engine_result, 'tecNO_DST_INSUF_XRP');
      done();
    })
    .submit();
  });

  // Also test transaction becomes lost after tecNO_DST.
  test("credit_limit to non-existent account = tecNO_DST", function (done) {
    $.remote.transaction()
    .ripple_line_set("root", "100/USD/alice")
    .once('submitted', function (m) {
      //console.log("proposed: %s", JSON.stringify(m));
      assert.strictEqual(m.engine_result, 'tecNO_DST');
      done();
    })
    .submit();
  });

  test("credit_limit", function (done) {
    var self = this;

    var steps = [
      function (callback) {
        self.what = "Create accounts.";
        testutils.create_accounts($.remote, "root", "10000.0", ["alice", "bob", "mtgox"], callback);
      },

      function (callback) {
        self.what = "Check a non-existent credit limit.";

        $.remote.request_ripple_balance("alice", "mtgox", "USD", 'current')
        .on('ripple_state', function (m) {
          callback(new Error(m));
        })
        .on('error', function(m) {
          // console.log("error: %s", JSON.stringify(m));

          assert.strictEqual('remoteError', m.error);
          assert.strictEqual('entryNotFound', m.remote.error);
          callback();
        })
        .request();
      },

      function (callback) {
        self.what = "Create a credit limit.";
        testutils.credit_limit($.remote, "alice", "800/USD/mtgox", callback);
      },

      function (callback) {
        $.remote.request_ripple_balance("alice", "mtgox", "USD", 'current')
        .on('ripple_state', function (m) {
          //                console.log("BALANCE: %s", JSON.stringify(m));
          //                console.log("account_balance: %s", m.account_balance.to_text_full());
          //                console.log("account_limit: %s", m.account_limit.to_text_full());
          //                console.log("peer_balance: %s", m.peer_balance.to_text_full());
          //                console.log("peer_limit: %s", m.peer_limit.to_text_full());
          assert(m.account_balance.equals("0/USD/alice"));
          assert(m.account_limit.equals("800/USD/mtgox"));
          assert(m.peer_balance.equals("0/USD/mtgox"));
          assert(m.peer_limit.equals("0/USD/alice"));

          callback();
        })
        .request();
      },

      function (callback) {
        self.what = "Modify a credit limit.";
        testutils.credit_limit($.remote, "alice", "700/USD/mtgox", callback);
      },

      function (callback) {
        $.remote.request_ripple_balance("alice", "mtgox", "USD", 'current')
        .on('ripple_state', function (m) {
          assert(m.account_balance.equals("0/USD/alice"));
          assert(m.account_limit.equals("700/USD/mtgox"));
          assert(m.peer_balance.equals("0/USD/mtgox"));
          assert(m.peer_limit.equals("0/USD/alice"));

          callback();
        })
        .request();
      },
      // // Set negative limit.
      // function (callback) {
      //   $.remote.transaction()
      //   .ripple_line_set("alice", "-1/USD/mtgox")
      //   .once('submitted', function (m) {
      //     assert.strictEqual('temBAD_LIMIT', m.engine_result);
      //     callback();
      //   })
      //   .submit();
      // },

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
        self.what = "Zero a credit limit.";
        testutils.credit_limit($.remote, "alice", "0/USD/mtgox", callback);
      },

      function (callback) {
        self.what = "Make sure line is deleted.";

        $.remote.request_ripple_balance("alice", "mtgox", "USD", 'current')
        .on('ripple_state', function (m) {
          // Used to keep lines.
          // assert(m.account_balance.equals("0/USD/alice"));
          // assert(m.account_limit.equals("0/USD/alice"));
          // assert(m.peer_balance.equals("0/USD/mtgox"));
          // assert(m.peer_limit.equals("0/USD/mtgox"));
          callback(new Error(m));
        })
        .on('error', function (m) {
          // console.log("error: %s", JSON.stringify(m));
          assert.strictEqual('remoteError', m.error);
          assert.strictEqual('entryNotFound', m.remote.error);

          callback();
        })
        .request();
      },
      // TODO Check in both owner books.
      function (callback) {
        self.what = "Set another limit.";
        testutils.credit_limit($.remote, "alice", "600/USD/bob", callback);
      },

      function (callback) {
        self.what = "Set limit on other side.";
        testutils.credit_limit($.remote, "bob", "500/USD/alice", callback);
      },

      function (callback) {
        self.what = "Check ripple_line's state from alice's pov.";

        $.remote.request_ripple_balance("alice", "bob", "USD", 'current')
        .on('ripple_state', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));

          assert(m.account_balance.equals("0/USD/alice"));
          assert(m.account_limit.equals("600/USD/bob"));
          assert(m.peer_balance.equals("0/USD/bob"));
          assert(m.peer_limit.equals("500/USD/alice"));

          callback();
        })
        .request();
      },

      function (callback) {
        self.what = "Check ripple_line's state from bob's pov.";

        $.remote.request_ripple_balance("bob", "alice", "USD", 'current')
        .on('ripple_state', function (m) {
          assert(m.account_balance.equals("0/USD/bob"));
          assert(m.account_limit.equals("500/USD/alice"));
          assert(m.peer_balance.equals("0/USD/alice"));
          assert(m.peer_limit.equals("600/USD/bob"));

          callback();
        })
        .request();
      }
    ]

    async.waterfall(steps, function(error) {
      assert(!error, self.what);
      done();
    });
  });
});

suite('Sending future', function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test('direct ripple', function(done) {
    var self = this;

    // $.remote.set_trace();

    var steps = [
      function (callback) {
        self.what = "Create accounts.";
        testutils.create_accounts($.remote, "root", "10000.0", ["alice", "bob"], callback);
      },

      function (callback) {
        self.what = "Set alice's limit.";
        testutils.credit_limit($.remote, "alice", "600/USD/bob", callback);
      },

      function (callback) {
        self.what = "Set bob's limit.";
        testutils.credit_limit($.remote, "bob", "700/USD/alice", callback);
      },

      function (callback) {
        self.what = "Set alice send bob partial with alice as issuer.";

        $.remote.transaction()
        .payment('alice', 'bob', "24/USD/alice")
        .once('submitted', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));
          callback(m.engine_result !== 'tesSUCCESS');
        })
        .once('final', function (m) {
          assert(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },

      function (callback) {
        self.what = "Verify balance.";

        $.remote.request_ripple_balance("alice", "bob", "USD", 'current')
        .once('ripple_state', function (m) {
          assert(m.account_balance.equals("-24/USD/alice"));
          assert(m.peer_balance.equals("24/USD/bob"));

          callback();
        })
        .request();
      },

      function (callback) {
        self.what = "Set alice send bob more with bob as issuer.";

        $.remote.transaction()
        .payment('alice', 'bob', "33/USD/bob")
        .once('submitted', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));
          callback(m.engine_result !== 'tesSUCCESS');
        })
        .once('final', function (m) {
          assert(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },

      function (callback) {
        self.what = "Verify balance from bob's pov.";

        $.remote.request_ripple_balance("bob", "alice", "USD", 'current')
        .once('ripple_state', function (m) {
          assert(m.account_balance.equals("57/USD/bob"));
          assert(m.peer_balance.equals("-57/USD/alice"));

          callback();
        })
        .request();
      },

      function (callback) {
        self.what = "Bob send back more than sent.";

        $.remote.transaction()
        .payment('bob', 'alice', "90/USD/bob")
        .once('submitted', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));
          callback(m.engine_result !== 'tesSUCCESS');
        })
        .once('final', function (m) {
          assert(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },

      function (callback) {
        self.what = "Verify balance from alice's pov: 1";

        $.remote.request_ripple_balance("alice", "bob", "USD", 'current')
        .once('ripple_state', function (m) {
          assert(m.account_balance.equals("33/USD/alice"));

          callback();
        })
        .request();
      },

      function (callback) {
        self.what = "Alice send to limit.";

        $.remote.transaction()
        .payment('alice', 'bob', "733/USD/bob")
        .once('submitted', function (m) {
          // console.log("submitted: %s", JSON.stringify(m));
          callback(m.engine_result !== 'tesSUCCESS');
        })
        .once('final', function (m) {
          assert(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },

      function (callback) {
        self.what = "Verify balance from alice's pov: 2";

        $.remote.request_ripple_balance("alice", "bob", "USD", 'current')
        .once('ripple_state', function (m) {
          assert(m.account_balance.equals("-700/USD/alice"));

          callback();
        })
        .request();
      },

      function (callback) {
        self.what = "Bob send to limit.";

        $.remote.transaction()
        .payment('bob', 'alice', "1300/USD/bob")
        .once('submitted', function (m) {
          // console.log("submitted: %s", JSON.stringify(m));
          callback(m.engine_result !== 'tesSUCCESS');
        })
        .once('final', function (m) {
          assert(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },

      function (callback) {
        self.what = "Verify balance from alice's pov: 3";

        $.remote.request_ripple_balance("alice", "bob", "USD", 'current')
        .once('ripple_state', function (m) {
          assert(m.account_balance.equals("600/USD/alice"));

          callback();
        })
        .request();
      },

      function (callback) {
        // If this gets applied out of order, it could stop the big payment.
        self.what = "Bob send past limit.";

        $.remote.transaction()
        .payment('bob', 'alice', "1/USD/bob")
        .once('submitted', function (m) {
          // console.log("submitted: %s", JSON.stringify(m));
          callback(m.engine_result !== 'tecPATH_DRY');
        })
        .submit();
      },

      function (callback) {
        self.what = "Verify balance from alice's pov: 4";

        $.remote.request_ripple_balance("alice", "bob", "USD", 'current')
        .once('ripple_state', function (m) {
          assert(m.account_balance.equals("600/USD/alice"));

          callback();
        })
        .request();
      },

      //        function (callback) {
      //          // Make sure all is good after canonical ordering.
      //          self.what = "Close the ledger and check balance.";
      //
      //          $.remote
      //            .once('ledger_closed', function (message) {
      //                // console.log("LEDGER_CLOSED: A: %d: %s", ledger_closed_index, ledger_closed);
      //                callback();
      //              })
      //            .ledger_accept();
      //        },
      //        function (callback) {
      //          self.what = "Verify balance from alice's pov: 5";
      //
      //          $.remote.request_ripple_balance("alice", "bob", "USD", 'current')
      //            .once('ripple_state', function (m) {
      //                console.log("account_balance: %s", m.account_balance.to_text_full());
      //                console.log("account_limit: %s", m.account_limit.to_text_full());
      //                console.log("peer_balance: %s", m.peer_balance.to_text_full());
      //                console.log("peer_limit: %s", m.peer_limit.to_text_full());
      //
      //                assert(m.account_balance.equals("600/USD/alice"));
      //
      //                callback();
      //              })
      //            .request();
      //        },
    ]

    async.waterfall(steps, function(error) {
      assert(!error, self.what);
      done();
    });
  });
});

suite('Gateway', function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test("customer to customer with and without transfer fee", function (done) {
    var self = this;

    // $.remote.set_trace();

    var steps = [
      function (callback) {
        self.what = "Create accounts.";
        testutils.create_accounts($.remote, "root", "10000.0", ["alice", "bob", "mtgox"], callback);
      },

      function (callback) {
        self.what = "Set credit limits.";

        testutils.credit_limits($.remote, {
          "alice" : "100/AUD/mtgox",
          "bob"   : "100/AUD/mtgox",
        },
        callback);
      },

      function (callback) {
        self.what = "Distribute funds.";

        testutils.payments($.remote, {
          "mtgox" : [ "1/AUD/alice" ],
        },
        callback);
      },

      function (callback) {
        self.what = "Verify balances.";

        testutils.verify_balances($.remote, {
          "alice"   : "1/AUD/mtgox",
          "mtgox"   : "-1/AUD/alice",
        },
        callback);
      },

      function (callback) {
        self.what = "Alice sends Bob 1 AUD";

        $.remote.transaction()
        .payment("alice", "bob", "1/AUD/mtgox")
        .once('proposed', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));

          callback(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },

      function (callback) {
        self.what = "Verify balances 2.";

        testutils.verify_balances($.remote, {
          "alice"   : "0/AUD/mtgox",
          "bob"     : "1/AUD/mtgox",
          "mtgox"   : "-1/AUD/bob",
        },
        callback);
      },

      function (callback) {
        self.what = "Set transfer rate.";

        $.remote.transaction()
        .account_set("mtgox")
        .transfer_rate(1e9*1.1)
        .once('proposed', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));
          callback(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },

      function (callback) {
        self.what = "Bob sends Alice 0.5 AUD";

        $.remote.transaction()
        .payment("bob", "alice", "0.5/AUD/mtgox")
        .send_max("0.55/AUD/mtgox") // !!! Very important.
        .once('proposed', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));

          callback(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },

      function (callback) {
        self.what = "Verify balances 3.";

        testutils.verify_balances($.remote, {
          "alice"   : "0.5/AUD/mtgox",
          "bob"     : "0.45/AUD/mtgox",
          "mtgox"   : [ "-0.5/AUD/alice","-0.45/AUD/bob" ],
        },
        callback);
      },
    ]

    async.waterfall(steps, function(error) {
      assert(!error, self.what);
      done();
    });
  });

  test("customer to customer, transfer fee, default path with and without specific issuer for Amount and SendMax", function (done) {

    var self = this;

    // $.remote.set_trace();

    var steps = [
      function (callback) {
        self.what = "Create accounts.";
        testutils.create_accounts($.remote, "root", "10000.0", ["alice", "bob", "mtgox"], callback);
      },

      function (callback) {
        self.what = "Set transfer rate.";

        $.remote.transaction()
        .account_set("mtgox")
        .transfer_rate(1e9*1.1)
        .once('submitted', function (m) {
          // console.log("submitted: %s", JSON.stringify(m));
          callback(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },

      function (callback) {
        self.what = "Set credit limits.";

        testutils.credit_limits($.remote, {
          "alice" : "100/AUD/mtgox",
          "bob"   : "100/AUD/mtgox",
        },
        callback);
      },

      function (callback) {
        self.what = "Distribute funds.";

        testutils.payments($.remote, {
          "mtgox" : [ "4.4/AUD/alice" ],
        },
        callback);
      },

      function (callback) {
        self.what = "Verify balances.";

        testutils.verify_balances($.remote, {
          "alice"   : "4.4/AUD/mtgox",
        },
        callback);
      },

      function (callback) {
        self.what = "Alice sends 1.1/AUD/mtgox Bob 1/AUD/mtgox";

        $.remote.transaction()
        .payment("alice", "bob", "1/AUD/mtgox")
        .send_max("1.1/AUD/mtgox")
        .once('submitted', function (m) {
          // console.log("submitted: %s", JSON.stringify(m));

          callback(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },

      function (callback) {
        self.what = "Verify balances 2.";

        testutils.verify_balances($.remote, {
          "alice"   : "3.3/AUD/mtgox",
          "bob"     : "1/AUD/mtgox",
        },
        callback);
      },

      function (callback) {
        self.what = "Alice sends 1.1/AUD/mtgox Bob 1/AUD/bob";

        $.remote.transaction()
        .payment("alice", "bob", "1/AUD/bob")
        .send_max("1.1/AUD/mtgox")
        .once('submitted', function (m) {
          // console.log("submitted: %s", JSON.stringify(m));

          callback(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },

      function (callback) {
        self.what = "Verify balances 3.";

        testutils.verify_balances($.remote, {
          "alice"   : "2.2/AUD/mtgox",
          "bob"     : "2/AUD/mtgox",
        },
        callback);
      },

      function (callback) {
        self.what = "Alice sends 1.1/AUD/alice Bob 1/AUD/mtgox";

        $.remote.transaction()
        .payment("alice", "bob", "1/AUD/mtgox")
        .send_max("1.1/AUD/alice")
        .once('submitted', function (m) {
          // console.log("submitted: %s", JSON.stringify(m));

          callback(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },

      function (callback) {
        self.what = "Verify balances 4.";

        testutils.verify_balances($.remote, {
          "alice"   : "1.1/AUD/mtgox",
          "bob"     : "3/AUD/mtgox",
        },
        callback);
      },

      function (callback) {
        // Must fail, doesn't know to use the mtgox
        self.what = "Alice sends 1.1/AUD/alice Bob 1/AUD/bob";

        $.remote.transaction()
        .payment("alice", "bob", "1/AUD/bob")
        .send_max("1.1/AUD/alice")
        .once('submitted', function (m) {
          // console.log("submitted: %s", JSON.stringify(m));

          callback(m.engine_result !== 'tecPATH_DRY');
        })
        .submit();
      },

      function (callback) {
        self.what = "Verify balances 5.";

        testutils.verify_balances($.remote, {
          "alice"   : "1.1/AUD/mtgox",
          "bob"     : "3/AUD/mtgox",
        },
        callback);
      }
    ]

    async.waterfall(steps, function(error) {
      assert(!error, self.what);
      done();
    });
  });

  test("subscribe test customer to customer with and without transfer fee", function (done) {
    var self = this;

    // $.remote.set_trace();

    var steps = [
      function (callback) {
        self.what = "Create accounts.";
        testutils.create_accounts($.remote, "root", "10000.0", ["alice", "bob", "mtgox"], callback);
      },

      function (callback) { testutils.ledger_close($.remote, callback); },

      function (callback) {
        self.what = "Set credit limits.";

        testutils.credit_limits($.remote, {
          "alice" : "100/AUD/mtgox",
          "bob"   : "100/AUD/mtgox",
        },
        callback);
      },

      function (callback) { testutils.ledger_close($.remote, callback); },

      function (callback) {
        self.what = "Distribute funds.";

        testutils.payments($.remote, {
          "mtgox" : [ "1/AUD/alice" ],
        },
        callback);
      },

      function (callback) { testutils.ledger_close($.remote, callback); },

      function (callback) {
        self.what = "Verify balances.";

        testutils.verify_balances($.remote, {
          "alice"   : "1/AUD/mtgox",
          "mtgox"   : "-1/AUD/alice",
        },
        callback);
      },

      function (callback) {
        self.what = "Alice sends Bob 1 AUD";

        $.remote.transaction()
        .payment("alice", "bob", "1/AUD/mtgox")
        .on('proposed', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));

          callback(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },

      function (callback) { testutils.ledger_close($.remote, callback); },

      function (callback) {
        self.what = "Verify balances 2.";

        testutils.verify_balances($.remote, {
          "alice"   : "0/AUD/mtgox",
          "bob"     : "1/AUD/mtgox",
          "mtgox"   : "-1/AUD/bob",
        },
        callback);
      },

      function (callback) {
        self.what = "Set transfer rate.";

        $.remote.transaction()
        .account_set("mtgox")
        .transfer_rate(1e9*1.1)
        .once('proposed', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));
          callback(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },

      function (callback) { testutils.ledger_close($.remote, callback); },

      function (callback) {
        self.what = "Bob sends Alice 0.5 AUD";

        $.remote.transaction()
        .payment("bob", "alice", "0.5/AUD/mtgox")
        .send_max("0.55/AUD/mtgox") // !!! Very important.
        .on('proposed', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));

          callback(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },

      function (callback) {
        self.what = "Verify balances 3.";

        testutils.verify_balances($.remote, {
          "alice"   : "0.5/AUD/mtgox",
          "bob"     : "0.45/AUD/mtgox",
          "mtgox"   : [ "-0.5/AUD/alice","-0.45/AUD/bob" ],
        },
        callback);
      },

      function (callback) {
        self.what  = "Subscribe and accept.";
        self.count = 0;
        self.found = 0;

        $.remote
        .on('transaction', function (m) {
          // console.log("ACCOUNT: %s", JSON.stringify(m));
          self.found = 1;
        })
        .on('ledger_closed', function (m) {
          // console.log("LEDGER_CLOSE: %d: %s", self.count, JSON.stringify(m));
          if (self.count) {
            callback(!self.found);
          } else {
            self.count  = 1;
            $.remote.ledger_accept();
          }
        })
        .request_subscribe().accounts("mtgox")
        .request();

        $.remote.ledger_accept();
      }
    ]

    async.waterfall(steps, function(error) {
      assert(!error, self.what);
      done();
    });
  });

  test("subscribe test: customer to customer with and without transfer fee: transaction retry logic", function (done) {

    var self = this;

    // $.remote.set_trace();

    var steps = [
      function (callback) {
        self.what = "Create accounts.";

        testutils.create_accounts($.remote, "root", "10000.0", ["alice", "bob", "mtgox"], callback);
      },

      function (callback) {
        self.what = "Set credit limits.";

        testutils.credit_limits($.remote,
                                {
                                  "alice" : "100/AUD/mtgox",
                                  "bob"   : "100/AUD/mtgox",
                                },
                                callback);
      },

      function (callback) {
        self.what = "Distribute funds.";

        testutils.payments($.remote, {
          "mtgox" : [ "1/AUD/alice" ],
        },
        callback);
      },

      function (callback) {
        self.what = "Verify balances.";

        testutils.verify_balances($.remote, {
          "alice"   : "1/AUD/mtgox",
          "mtgox"   : "-1/AUD/alice",
        },
        callback);
      },

      function (callback) {
        self.what = "Alice sends Bob 1 AUD";

        $.remote.transaction()
        .payment("alice", "bob", "1/AUD/mtgox")
        .on('proposed', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));

          callback(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },

      function (callback) {
        self.what = "Verify balances 2.";

        testutils.verify_balances($.remote, {
          "alice"   : "0/AUD/mtgox",
          "bob"     : "1/AUD/mtgox",
          "mtgox"   : "-1/AUD/bob",
        },
        callback);
      },

      //          function (callback) {
      //            self.what = "Set transfer rate.";
      //
      //            $.remote.transaction()
      //              .account_set("mtgox")
      //              .transfer_rate(1e9*1.1)
      //              .once('proposed', function (m) {
      //                  // console.log("proposed: %s", JSON.stringify(m));
      //                  callback(m.engine_result !== 'tesSUCCESS');
      //                })
      //              .submit();
      //          },

      // We now need to ensure that all prior transactions have executed before
      // the next transaction is submitted, as txn application logic has
      // changed.
      function(next){$.remote.ledger_accept(function(){next();});},

      function (callback) {
        self.what = "Bob sends Alice 0.5 AUD";

        $.remote.transaction()
        .payment("bob", "alice", "0.5/AUD/mtgox")
        .on('proposed', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));

          callback(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },

      function (callback) {
        self.what = "Verify balances 3.";

        testutils.verify_balances($.remote, {
          "alice"   : "0.5/AUD/mtgox",
          "bob"     : "0.5/AUD/mtgox",
          "mtgox"   : [ "-0.5/AUD/alice","-0.5/AUD/bob" ],
        },
        callback);
      },

      function (callback) {
        self.what  = "Subscribe and accept.";
        self.count = 0;
        self.found = 0;

        $.remote
        .on('transaction', function (m) {
          // console.log("ACCOUNT: %s", JSON.stringify(m));
          self.found  = 1;
        })
        .on('ledger_closed', function (m) {
          // console.log("LEDGER_CLOSE: %d: %s", self.count, JSON.stringify(m));

          if (self.count) {
            callback(!self.found);
          } else {
            self.count  = 1;
            $.remote.ledger_accept();
          }
        })
        .request_subscribe().accounts("mtgox")
        .request();

        $.remote.ledger_accept();
      },
      function (callback) {
        self.what = "Verify balances 4.";

        testutils.verify_balances($.remote, {
          "alice"   : "0.5/AUD/mtgox",
          "bob"     : "0.5/AUD/mtgox",
          "mtgox"   : [ "-0.5/AUD/alice","-0.5/AUD/bob" ],
        },
        callback);
      },
    ]

    async.waterfall(steps, function (error) {
      assert(!error, self.what);
      done();
    });
  });
});


suite('Indirect ripple', function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test("indirect ripple", function (done) {
    var self = this;

    // $.remote.set_trace();

    var steps = [
      function (callback) {
        self.what = "Create accounts.";

        testutils.create_accounts($.remote, "root", "10000.0", ["alice", "bob", "mtgox"], callback);
      },
      function (callback) {
        self.what = "Set credit limits.";

        testutils.credit_limits($.remote, {
          "alice" : "600/USD/mtgox",
          "bob"   : "700/USD/mtgox",
        },
        callback);
      },
      function (callback) {
        self.what = "Distribute funds.";

        testutils.payments($.remote, {
          "mtgox" : [ "70/USD/alice", "50/USD/bob" ],
        },
        callback);
      },
      function (callback) {
        self.what = "Verify alice balance with mtgox.";

        testutils.verify_balance($.remote, "alice", "70/USD/mtgox", callback);
      },
      function (callback) {
        self.what = "Verify bob balance with mtgox.";

        testutils.verify_balance($.remote, "bob", "50/USD/mtgox", callback);
      },
      function (callback) {
        self.what = "Alice sends more than has to issuer: 100 out of 70";

        $.remote.transaction()
        .payment("alice", "mtgox", "100/USD/mtgox")
        .once('submitted', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));
          callback(m.engine_result !== 'tecPATH_PARTIAL');
        })
        .submit();
      },
      function (callback) {
        self.what = "Alice sends more than has to bob: 100 out of 70";

        $.remote.transaction()
        .payment("alice", "bob", "100/USD/mtgox")
        .once('submitted', function (m) {
          //console.log("proposed: %s", JSON.stringify(m));
          callback(m.engine_result !== 'tecPATH_PARTIAL');
        })
        .submit();
      }
    ]

    async.waterfall(steps, function(error) {
      assert(!error, self.what);
      done();
    });
  });

  test("indirect ripple with path", function (done) {
    var self = this;

    var steps = [
      function (callback) {
        self.what = "Create accounts.";

        testutils.create_accounts($.remote, "root", "10000.0", ["alice", "bob", "mtgox"], callback);
      },
      function (callback) {
        self.what = "Set credit limits.";

        testutils.credit_limits($.remote, {
          "alice" : "600/USD/mtgox",
          "bob"   : "700/USD/mtgox",
        },
        callback);
      },
      function (callback) {
        self.what = "Distribute funds.";

        testutils.payments($.remote, {
          "mtgox" : [ "70/USD/alice", "50/USD/bob" ],
        },
        callback);
      },
      function (callback) {
        self.what = "Alice sends via a path";

        $.remote.transaction()
        .payment("alice", "bob", "5/USD/mtgox")
        .pathAdd( [ { account: "mtgox" } ])
        .on('proposed', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));

          callback(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },
      function (callback) {
        self.what = "Verify alice balance with mtgox.";

        testutils.verify_balance($.remote, "alice", "65/USD/mtgox", callback);
      },
      function (callback) {
        self.what = "Verify bob balance with mtgox.";

        testutils.verify_balance($.remote, "bob", "55/USD/mtgox", callback);
      }
    ]

    async.waterfall(steps, function(error) {
      assert(!error, self.what);
      done();
    });
  });

  test("indirect ripple with multi path", function (done) {
    var self = this;

    var steps = [
      function (callback) {
        self.what = "Create accounts.";

        testutils.create_accounts($.remote, "root", "10000.0", ["alice", "bob", "carol", "amazon", "mtgox"], callback);
      },
      function (callback) {
        self.what = "Set credit limits.";

        testutils.credit_limits($.remote, {
          "amazon"  : "2000/USD/mtgox",
          "bob"   : [ "600/USD/alice", "1000/USD/mtgox" ],
          "carol" : [ "700/USD/alice", "1000/USD/mtgox" ],
        },
        callback);
      },
      function (callback) {
        self.what = "Distribute funds.";

        testutils.payments($.remote, {
          "mtgox" : [ "100/USD/bob", "100/USD/carol" ],
        },
        callback);
      },
      function (callback) {
        self.what = "Alice pays amazon via multiple paths";

        $.remote.transaction()
        .payment("alice", "amazon", "150/USD/mtgox")
        .pathAdd( [ { account: "bob" } ])
        .pathAdd( [ { account: "carol" } ])
        .on('proposed', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));

          callback(m.engine_result !== 'tesSUCCESS');
        })
        .submit();
      },
      function (callback) {
        self.what = "Verify balances.";

        testutils.verify_balances($.remote, {
          "alice"   : [ "-100/USD/bob", "-50/USD/carol" ],
          "amazon"  : "150/USD/mtgox",
          "bob"     : "0/USD/mtgox",
          "carol"   : "50/USD/mtgox",
        },
        callback);
      },
    ]

    async.waterfall(steps, function(error) {
      assert(!error, self.what);
      done();
    });
  });

  test("indirect ripple with path and transfer fee", function (done) {
    var self = this;

    var steps = [
      function (callback) {
        self.what = "Create accounts.";

        testutils.create_accounts($.remote, "root", "10000.0", ["alice", "bob", "carol", "amazon", "mtgox"], callback);
      },
      function (callback) {
        self.what = "Set mtgox transfer rate.";

        testutils.transfer_rate($.remote, "mtgox", 1.1e9, callback);
      },
      function (callback) {
        self.what = "Set credit limits.";

        testutils.credit_limits($.remote, {
          "amazon"  : "2000/USD/mtgox",
          "bob"   : [ "600/USD/alice", "1000/USD/mtgox" ],
          "carol" : [ "700/USD/alice", "1000/USD/mtgox" ],
        },
        callback);
      },
      function (callback) {
        self.what = "Distribute funds.";

        testutils.payments($.remote, {
          "mtgox" : [ "100/USD/bob", "100/USD/carol" ],
        },
        callback);
      },
      function (callback) {
        self.what = "Alice pays amazon via multiple paths";

        $.remote.transaction()
        .payment("alice", "amazon", "150/USD/mtgox")
        .send_max("200/USD/alice")
        .pathAdd( [ { account: "bob" } ])
        .pathAdd( [ { account: "carol" } ])
        .on('proposed', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));

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

        // 65.00000000000001 is correct.
        // This is result of limited precision.
        testutils.verify_balances($.remote, {
          "alice"   : [ "-100/USD/bob", "-65.00000000000001/USD/carol" ],
          "amazon"  : "150/USD/mtgox",
          "bob"     : "0/USD/mtgox",
          "carol"   : "35/USD/mtgox",
        },
        callback);
      }
    ]

    async.waterfall(steps, function(error) {
      assert(!error, self.what);
      done();
    });
  })
});

suite('Invoice ID', function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test('set InvoiceID on payment', function(done) {
    var self = this;

    var steps = [
      function (callback) {
        self.what = 'Create accounts';
        testutils.create_accounts($.remote, 'root', '10000.0', [ 'alice' ], callback);
      },

      function (callback) {
        self.what = 'Send a payment with InvoiceID';

        var tx = $.remote.transaction();
        tx.payment('root', 'alice', '10000');
        tx.invoiceID('DEADBEEF');

        tx.once('submitted', function(m) {
          assert.strictEqual(m.engine_result, 'tesSUCCESS');
          assert.strictEqual(m.tx_json.InvoiceID, 'DEADBEEF00000000000000000000000000000000000000000000000000000000');
          callback();
        });

        tx.submit();
      }
    ]

    async.series(steps, function(err) {
      assert(!err, self.what + ': ' + err);
      done();
    });
  });
});

// vim:sw=2:sts=2:ts=8:et
