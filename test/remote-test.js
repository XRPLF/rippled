var buster  = require("buster");

var config  = require("./config.js");
var server  = require("./server.js");
var amount  = require("../js/amount.js");
var remote  = require("../js/remote.js");

var Amount  = amount.Amount;

var fastTearDown  = true;

// How long to wait for server to start.
var serverDelay = 1500;

buster.testRunner.timeout = 5000;
 
buster.testCase("Remote functions", {
  'setUp' :
    function (done) {
      server.start("alpha",
	function (e) {
	  buster.refute(e);

	  alpha   = remote.remoteConfig(config, "alpha");

	  alpha.connect(function (stat) {
	      buster.assert(1 == stat);	      // OPEN
	      done();
	    }, serverDelay);
      });
    },

  'tearDown' :
    function (done) {
      if (fastTearDown) {
	// Fast tearDown
	server.stop("alpha", function (e) {
	  buster.refute(e);
	  done();
	});
      }
      else {
	alpha.disconnect(function (stat) {
	    buster.assert(3 == stat);		// CLOSED

	    server.stop("alpha", function (e) {
	      buster.refute(e);
	      done();
	    });
	  });
      }
    },

  'request_ledger_current' :
    function (done) {
      alpha.request_ledger_current().on('success', function (m) {
	  console.log(m);

	  buster.assert.equals(m.ledger_current_index, 3);
	  done();
	}).on('error', function(m) {
	  console.log(m);

	  buster.assert(false);
	}).request();
    },

  'request_ledger_closed' :
    function (done) {
      alpha.request_ledger_closed().on('success', function (m) {
	  console.log("result: %s", JSON.stringify(m));

	  buster.assert.equals(m.ledger_closed_index, 2);
	  done();
	}).on('error', function(m) {
	  console.log("error: %s", m);

	  buster.assert(false);
	}).request();
    },

  'manual account_root success' :
    function (done) {
      alpha.request_ledger_closed().on('success', function (r) {
	  // console.log("result: %s", JSON.stringify(r));

	  alpha
	    .request_ledger_entry('account_root')
	    .ledger(r.ledger_closed)
	    .account_root("rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")
	    .on('success', function (r) {
		// console.log("account_root: %s", JSON.stringify(r));

		buster.assert('node' in r);
		done();
	      }).on('error', function(m) {
		console.log("error: %s", m);

		buster.assert(false);
	      }).request();
	}).on('error', function(m) {
	  console.log("error: %s", m);

	  buster.assert(false);
	}).request();
    },

  'account_root remote malformedAddress' :
    function (done) {
      alpha.request_ledger_closed().on('success', function (r) {
	  console.log("result: %s", JSON.stringify(r));

	  alpha
	    .request_ledger_entry('account_root')
	    .ledger(r.ledger_closed)
	    .account_root("zHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")
	    .on('success', function (r) {
		// console.log("account_root: %s", JSON.stringify(r));

		buster.assert(false);
	      }).on('error', function(m) {
		console.log("error: %s", m);

		buster.assert.equals(m.error, 'remoteError');
		buster.assert.equals(m.remote.error, 'malformedAddress');
		done();
	      }).request();
	}).on('error', function(m) {
	  console.log("error: %s", m);

	  buster.assert(false);
	}).request();
    },

  'account_root entryNotFound' :
    function (done) {
      alpha.request_ledger_closed().on('success', function (r) {
	  console.log("result: %s", JSON.stringify(r));

	  alpha
	    .request_ledger_entry('account_root')
	    .ledger(r.ledger_closed)
	    .account_root(config.accounts.alice.account)
	    .on('success', function (r) {
		// console.log("account_root: %s", JSON.stringify(r));

		buster.assert(false);
	      }).on('error', function(m) {
		console.log("error: %s", m);

		buster.assert.equals(m.error, 'remoteError');
		buster.assert.equals(m.remote.error, 'entryNotFound');
		done();
	      }).request();
	}).on('error', function(m) {
	  console.log("error: %s", m);

	  buster.assert(false);
	}).request();
    },

  'ledger_entry index' :
    function (done) {
      alpha.request_ledger_closed().on('success', function (r) {
	  console.log("result: %s", JSON.stringify(r));

	  alpha
	    .request_ledger_entry('index')
	    .ledger(r.ledger_closed)
	    .account_root(config.accounts.alice.account)
	    .index("2B6AC232AA4C4BE41BF49D2459FA4A0347E1B543A4C92FCEE0821C0201E2E9A8")
	    .on('success', function (r) {
		// console.log("account_root: %s", JSON.stringify(r));

		buster.assert('node_binary' in r);
		done();
	      }).on('error', function(m) {
		console.log("error: %s", m);

		buster.assert(false);
	      }).request();
	}).on('error', function(m) {
	  console.log(m);

	  buster.assert(false);
	}).request();
    },

  'create account' :
    function (done) {
      alpha.transaction()
	.payment('root', 'alice', Amount.from_json("10000"))
	.flags('CreateAccount')
	.on('success', function (r) {
	    // console.log("account_root: %s", JSON.stringify(r));

	    // Need to verify account and balance.
	    done();
	  }).on('error', function(m) {
	    console.log("error: %s", m);

	    buster.assert(false);
	  }).submit();
    },
});

// vim:sw=2:sts=2:ts=8
