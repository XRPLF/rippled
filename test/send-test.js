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
  'setUp' : testutils.test_setup,
  'tearDown' : testutils.test_teardown,

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

  "// credit_limit" :
    function (done) {
      var self = this;
      this.remote.set_trace();

      async.waterfall([
	  function (callback) {
	    this.what = "Create account.";

	    testutils.create_accounts(self.remote, "root", "10000", ["alice", "bob", "mtgox"], callback);
	  },
	  function (callback) {
	    this.what = "Check a non-existant credit limit.";

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
	    this.what = "Create a credit limit.";

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
	    this.what = "Modify a credit limit.";

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
	    this.what = "Zero a credit limit.";

	    testutils.credit_limit(self.remote, "alice", "0/USD/mtgox", callback);
	  },
	  function (callback) {
	    this.what = "Make sure still exists.";

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
	  // Check in both owner books.
	  // Set limit on other side.
	  // Set negative limit.
	  //function (callback) {
	  //  testutils.credit_limit(self.remote, "alice", "-1/USD/mtgox", callback);
	  //},
	], function (error) {
	  buster.refute(error, this.what);
	  done();
	});
    }
});

// vim:sw=2:sts=2:ts=8
