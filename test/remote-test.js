var buster  = require("buster");

var config  = require("./config.js");
var server  = require("./server.js");
var amount  = require("../js/amount.js");
var remote  = require("../js/remote.js");

var Amount  = amount.Amount;

var fastTearDown  = true;

// How long to wait for server to start.
var serverDelay = 1500;	  // XXX Not implemented.

buster.testRunner.timeout = 5000;
 
buster.testCase("Remote functions", {
  'setUp' :
    function (done) {
      server.start("alpha",
	function (e) {
	  buster.refute(e);

	  alpha   = remote.remoteConfig(config, "alpha");

	  alpha
	    .once('ledger_closed', done)
	    .connect();
      });
    },

  'tearDown' :
    function (done) {
      alpha
	.on('disconnected', function () {
	    server.stop("alpha", function (e) {
	      buster.refute(e);
	      done();
	    });
	  })
	.connect(false);
    },

  'request_ledger_current' :
    function (done) {
      alpha.request_ledger_current().on('success', function (m) {
	  console.log(m);

	  buster.assert.equals(m.ledger_current_index, 3);
	  done();
	})
      .on('error', function(m) {
	  console.log(m);

	  buster.assert(false);
	})
      .request();
    },

  'request_ledger_closed' :
    function (done) {
      alpha.request_ledger_closed().on('success', function (m) {
	  console.log("result: %s", JSON.stringify(m));

	  buster.assert.equals(m.ledger_closed_index, 2);
	  done();
	})
      .on('error', function(m) {
	  console.log("error: %s", m);

	  buster.assert(false);
	})
      .request();
    },

  'manual account_root success' :
    function (done) {
      alpha.request_ledger_closed().on('success', function (r) {
	  // console.log("result: %s", JSON.stringify(r));

	  alpha
	    .request_ledger_entry('account_root')
	    .ledger_closed(r.ledger_closed)
	    .account_root("rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")
	    .on('success', function (r) {
		// console.log("account_root: %s", JSON.stringify(r));

		buster.assert('node' in r);
		done();
	      })
	    .on('error', function(m) {
		console.log("error: %s", m);

		buster.assert(false);
	      })
	    .request();
	})
      .on('error', function(m) {
	  console.log("error: %s", m);

	  buster.assert(false);
	})
      .request();
    },

  'account_root remote malformedAddress' :
    function (done) {
      alpha.request_ledger_closed().on('success', function (r) {
	  console.log("result: %s", JSON.stringify(r));

	  alpha
	    .request_ledger_entry('account_root')
	    .ledger_closed(r.ledger_closed)
	    .account_root("zHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")
	    .on('success', function (r) {
		// console.log("account_root: %s", JSON.stringify(r));

		buster.assert(false);
	      })
	    .on('error', function(m) {
		console.log("error: %s", m);

		buster.assert.equals(m.error, 'remoteError');
		buster.assert.equals(m.remote.error, 'malformedAddress');
		done();
	      })
	    .request();
	})
      .on('error', function(m) {
	  console.log("error: %s", m);

	  buster.assert(false);
	})
      .request();
    },

  'account_root entryNotFound' :
    function (done) {
      alpha.request_ledger_closed().on('success', function (r) {
	  console.log("result: %s", JSON.stringify(r));

	  alpha
	    .request_ledger_entry('account_root')
	    .ledger_closed(r.ledger_closed)
	    .account_root(config.accounts.alice.account)
	    .on('success', function (r) {
		// console.log("account_root: %s", JSON.stringify(r));

		buster.assert(false);
	      })
	    .on('error', function(m) {
		console.log("error: %s", m);

		buster.assert.equals(m.error, 'remoteError');
		buster.assert.equals(m.remote.error, 'entryNotFound');
		done();
	      })
	    .request();
	})
      .on('error', function(m) {
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
	    .ledger_closed(r.ledger_closed)
	    .account_root(config.accounts.alice.account)
	    .index("2B6AC232AA4C4BE41BF49D2459FA4A0347E1B543A4C92FCEE0821C0201E2E9A8")
	    .on('success', function (r) {
		// console.log("account_root: %s", JSON.stringify(r));

		buster.assert('node_binary' in r);
		done();
	      })
	    .on('error', function(m) {
		console.log("error: %s", m);

		buster.assert(false);
	      }).
	    request();
	})
      .on('error', function(m) {
	  console.log(m);

	  buster.assert(false);
	})
      .request();
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
	  })
	.on('error', function(m) {
	    console.log("error: %s", m);

	    buster.assert(false);
	  })
	.submit();
    },

  "create account final" :
    function (done) {
      var   got_proposed;
      var   got_success;

      alpha.transaction()
	.payment('root', 'alice', Amount.from_json("10000"))
	.flags('CreateAccount')
	.on('success', function (r) {
	    console.log("create_account: %s", JSON.stringify(r));

	    got_success	= true;
	  })
	.on('error', function (m) {
	    console.log("error: %s", m);

	    buster.assert(false);
	  })
	.on('final', function (m) {
	    console.log("final: %s", JSON.stringify(m));

	    buster.assert(got_success && got_proposed);
	    done();
	  })
	.on('proposed', function (m) {
	    console.log("proposed: %s", JSON.stringify(m));

	    // buster.assert.equals(m.result, 'terNO_DST');
	    buster.assert.equals(m.result, 'tesSUCCESS');

	    got_proposed  = true;

	    alpha.ledger_accept();
	  })
	.on('status', function (s) {
	    console.log("status: %s", JSON.stringify(s));
	  })
	.submit();
    },
});

// vim:sw=2:sts=2:ts=8
