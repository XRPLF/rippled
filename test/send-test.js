var buster  = require("buster");

var Amount = require("../js/amount.js").Amount;
var Remote  = require("../js/remote.js").Remote;
var Server  = require("./server.js").Server;

var testutils  = require("./testutils.js");

// How long to wait for server to start.
var serverDelay = 1500;

buster.testRunner.timeout = 5000;

buster.testCase("Sending", {
  'setUp' : testutils.test_setup,
  'tearDown' : testutils.test_teardown,

  "send to non-existant account without create." :
    function (done) {
      var self	  = this;
      var ledgers = 20;
      var got_proposed;

      this.remote.transaction()
	.payment('root', 'alice', Amount.from_json("10000"))
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
});

// vim:sw=2:sts=2:ts=8
