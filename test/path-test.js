var async   = require("async");
var buster  = require("buster");

var Amount = require("../src/js/amount.js").Amount;
var Remote  = require("../src/js/remote.js").Remote;
var Server  = require("./server.js").Server;

var testutils  = require("./testutils.js");

require("../src/js/amount.js").config = require("./config.js");
require("../src/js/remote.js").config = require("./config.js");

buster.testRunner.timeout = 5000;

buster.testCase("Path finding", {
  'setUp' : testutils.build_setup(),
  'tearDown' : testutils.build_teardown(),

  "path find" :
    function (done) {
      var self = this;

      async.waterfall([
	  function (callback) {
	    self.what = "Create accounts.";

	    testutils.create_accounts(self.remote, "root", "10000", ["alice", "bob", "mtgox"], callback);
	  },
	  function (callback) {
	    self.what = "Set credit limits.";

	    testutils.credit_limits(self.remote,
	      {
		"alice" : "600/USD/mtgox",
		"bob"	: "700/USD/mtgox",
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
	    self.what = "Find path from alice to mtgox";

	    self.remote.request_ripple_path_find("alice", "bob", "5/USD/mtgox",
	      [ { 'currency' : "USD" } ])
	      .on('success', function (m) {
		  console.log("proposed: m", JSON.stringify(m));

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
// vim:sw=2:sts=2:ts=8
