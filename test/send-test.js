var async   = require("async");
var buster  = require("buster");

var Amount = require("../js/amount.js").Amount;
var Remote  = require("../js/remote.js").Remote;
var Server  = require("./server.js").Server;

var testutils  = require("./testutils.js");

// How long to wait for server to start.
var serverDelay = 1500;

buster.testRunner.timeout = 3000;

buster.testCase("Sending", {
  'setUp' : testutils.build_setup(),
  'tearDown' : testutils.build_teardown(),

  "send XNS to non-existant account without create." :
    function (done) {
      var self	  = this;
      var ledgers = 20;
      var got_proposed;

      this.remote.transaction()
	.payment('root', 'alice', "10000")
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

	    buster.assert.equals(m.result, 'terNO_DST');

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
  "credit_limit to non-existant account = terNO_DST" :
    function (done) {
      this.remote.transaction()
	.ripple_line_set("root", "100/USD/alice")
	.on('proposed', function (m) {
	    // console.log("proposed: %s", JSON.stringify(m));

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

	    testutils.create_accounts(self.remote, "root", "10000", ["alice", "bob", "mtgox"], callback);
	  },
	  function (callback) {
	    self.what = "Check a non-existant credit limit.";

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
//		  console.log("BALANCE: %s", JSON.stringify(m));
//		  console.log("account_balance: %s", m.account_balance.to_text_full());
//		  console.log("account_limit: %s", m.account_limit.to_text_full());
//		  console.log("issuer_balance: %s", m.issuer_balance.to_text_full());
//		  console.log("issuer_limit: %s", m.issuer_limit.to_text_full());
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
	  function (callback) {
	    self.what = "Zero a credit limit.";

	    testutils.credit_limit(self.remote, "alice", "0/USD/mtgox", callback);
	  },
	  function (callback) {
	    self.what = "Make sure still exists.";

	    self.remote.request_ripple_balance("alice", "mtgox", "USD", 'CURRENT')
	      .on('ripple_state', function (m) {
		  buster.assert(m.account_balance.equals("0/USD/alice"));
		  buster.assert(m.account_limit.equals("0/USD/alice"));
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
  'setUp' : testutils.build_setup(),
  'tearDown' : testutils.build_teardown(),

  "direct ripple" :
    function (done) {
      var self = this;

      // self.remote.set_trace();

      async.waterfall([
	  function (callback) {
	    self.what = "Create accounts.";

	    testutils.create_accounts(self.remote, "root", "10000", ["alice", "bob"], callback);
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
		  callback(m.result != 'tesSUCCESS');
		})
	      .once('final', function (m) {
		  buster.assert(m.result != 'tesSUCCESS');
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
		  callback(m.result != 'tesSUCCESS');
		})
	      .once('final', function (m) {
		  buster.assert(m.result != 'tesSUCCESS');
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
		  callback(m.result != 'tesSUCCESS');
		})
	      .once('final', function (m) {
		  buster.assert(m.result != 'tesSUCCESS');
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
		  callback(m.result != 'tesSUCCESS');
		})
	      .once('final', function (m) {
		  buster.assert(m.result != 'tesSUCCESS');
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
		  callback(m.result != 'tesSUCCESS');
		})
	      .once('final', function (m) {
		  buster.assert(m.result != 'tesSUCCESS');
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
		  callback(m.result != 'tepPATH_DRY');
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
//	  function (callback) {
//	    // Make sure all is good after canonical ordering.
//	    self.what = "Close the ledger and check balance.";
//
//	    self.remote
//	      .once('ledger_closed', function (ledger_closed, ledger_closed_index) {
//		  // console.log("LEDGER_CLOSED: A: %d: %s", ledger_closed_index, ledger_closed);
//		  callback();
//		})
//	      .ledger_accept();
//	  },
//	  function (callback) {
//	    self.what = "Verify balance from alice's pov.";
//
//	    self.remote.request_ripple_balance("alice", "bob", "USD", 'CURRENT')
//	      .once('ripple_state', function (m) {
//		  console.log("account_balance: %s", m.account_balance.to_text_full());
//		  console.log("account_limit: %s", m.account_limit.to_text_full());
//		  console.log("issuer_balance: %s", m.issuer_balance.to_text_full());
//		  console.log("issuer_limit: %s", m.issuer_limit.to_text_full());
//
//		  buster.assert(m.account_balance.equals("600/USD/alice"));
//
//		  callback();
//		})
//	      .request();
//	  },
	], function (error) {
	  buster.refute(error, self.what);
	  done();
	});
    },

    // Ripple without credit path.
    // Ripple with one-way credit path.
});

buster.testCase("Indirect ripple", {
  'setUp' : testutils.build_setup({ verbose: false, no_server: false }),
  'tearDown' : testutils.build_teardown(),

  "indirect ripple" :
    function (done) {
      var self = this;

      // self.remote.set_trace();

      async.waterfall([
	  function (callback) {
	    self.what = "Create accounts.";

	    testutils.create_accounts(self.remote, "root", "10000", ["alice", "bob", "mtgox"], callback);
	  },
	  function (callback) {
	    self.what = "Set alice's limit.";

	    testutils.credit_limit(self.remote, "alice", "600/USD/mtgox", callback);
	  },
	  function (callback) {
	    self.what = "Set bob's limit.";

	    testutils.credit_limit(self.remote, "bob", "700/USD/mtgox", callback);
	  },
	  function (callback) {
	    self.what = "Give alice some mtgox.";

	    testutils.payment(self.remote, "mtgox", "alice", "70/USD/mtgox", callback);
	  },
	  function (callback) {
	    self.what = "Give bob some mtgox.";

	    testutils.payment(self.remote, "mtgox", "bob", "50/USD/mtgox", callback);
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

		  callback(m.result != 'tepPATH_PARTIAL');
		})
	      .submit();
	  },
	  function (callback) {
	    self.what = "Alice sends more than has to bob: 100 out of 70";

	    self.remote.transaction()
	      .payment("alice", "bob", "100/USD/mtgox")
	      .on('proposed', function (m) {
		  // console.log("proposed: %s", JSON.stringify(m));

		  callback(m.result != 'tepPATH_PARTIAL');
		})
	      .submit();
	  },
	], function (error) {
	  buster.refute(error, self.what);
	  done();
	});
    },

    // Direct ripple without no liqudity.
    // Ripple without credit path.
    // Ripple with one-way credit path.
    // Transfer Fees
    // Use multiple paths.
});
// vim:sw=2:sts=2:ts=8
