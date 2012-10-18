var buster  = require("buster");

var config  = require("./config.js");
var server  = require("./server.js");
var amount  = require("../js/amount.js");
var remote  = require("../js/remote.js");

var Amount  = amount.Amount;

// How long to wait for server to start.
var serverDelay = 1500;

buster.testRunner.timeout = 5000;

buster.testCase("Sending", {
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

  "send to non-existant account without create." :
    function (done) {
      var got_proposed;
      var ledgers  = 20;

      alpha.transaction()
	.payment('root', 'alice', Amount.from_json("10000"))
	.on('success', function (r) {
	    // Transaction sent.

	    console.log("success: %s", JSON.stringify(r));
	  })
	.on('pending', function() {
	    // Moving ledgers along.
	    console.log("missing: %d", ledgers);

	    ledgers    -= 1;
	    if (ledgers) {
	      alpha.ledger_accept();
	    }
	    else {
	      buster.assert(false, "Final never received.");
	      done();
	    }
	  })
	.on('lost', function () {
	    // Transaction did not make it in.
	    console.log("lost");

	    buster.assert(true);
	    done();
	  })
	.on('proposed', function (m) {
	    // Transaction got an error.
	    console.log("proposed: %s", JSON.stringify(m));

	    buster.assert.equals(m.result, 'terNO_DST');

	    got_proposed  = true;

	    alpha.ledger_accept();    // Move it along.
	  })
	.on('final', function (m) {
	    console.log("final: %s", JSON.stringify(m));

	    buster.assert(false, "Should not have got a final.");
	    done();
	  })
	.on('error', function(m) {
	    console.log("error: %s", m);

	    buster.assert(false);
	  })
	.submit();
    },
});

// vim:sw=2:sts=2:ts=8
