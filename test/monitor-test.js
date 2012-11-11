var async   = require("async");
var buster  = require("buster");

var Amount = require("../src/js/amount.js").Amount;
var Remote  = require("../src/js/remote.js").Remote;
var Server  = require("./server.js").Server;

var testutils  = require("./testutils.js");

require("../src/js/amount.js").config = require("./config.js");
require("../src/js/remote.js").config = require("./config.js");

buster.testRunner.timeout = 5000;
 
buster.testCase("//Monitor account", {
  'setUp' : testutils.build_setup({ verbose: true }),
  'tearDown' : testutils.build_teardown(),

  "monitor root" :
    function (done) {
      var self = this;

      async.waterfall([
	  function (callback) {
	    self.what = "Create accounts.";

	    testutils.create_accounts(self.remote, "root", "10000", ["alice"], callback);
	  },
	  function (callback) {
	    self.what = "Close ledger.";

	    self.remote.once('ledger_closed', function (ledger_closed, ledger_index) {
		callback();
	      });

	    self.remote.ledger_accept();
	  },
	  function (callback) {
	    self.what = "Dumping root.";

	    testutils.account_dump(self.remote, "root", function (error) {
		buster.refute(error);
	    
		callback();
	      });
	  },
      ], function (error) {
	buster.refute(error, self.what);
	done();
      });
    },
});

// vim:sw=2:sts=2:ts=8
