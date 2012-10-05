var fs = require("fs");
var buster = require("buster");

var server = require("./server.js");
var remote = require("../js/remote.js");
var config = require("./config.js");

// How long to wait for server to start.
var serverDelay = 1500;

buster.testRunner.timeout = 5000;

buster.testCase("Standalone server startup", {
  "server start and stop": function (done) {
      server.start("alpha",
	function (e) {
	  buster.refute(e);
	  server.stop("alpha", function (e) {
	    buster.refute(e);
	    done();
	  });
	});
    }
});

buster.testCase("WebSocket connection", {
  'setUp' :
    function (done) {
      server.start("alpha",
	function (e) {
	  buster.refute(e);
	  done();
	}
      );
    },

  'tearDown' :
    function (done) {
      server.stop("alpha", function (e) {
	buster.refute(e);
	done();
      });
    },

  "websocket connect and disconnect" :
    function (done) {
      var alpha	= remote.remoteConfig(config, "alpha");

      alpha.connect(function (stat) {
	buster.assert(1 == stat);	    // OPEN

	alpha.disconnect(function (stat) {
	    buster.assert(3 == stat);	    // CLOSED
	    done();
	  });
	}, serverDelay);
    },
});

buster.testCase("Websocket commands", {
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
      alpha.disconnect(function (stat) {
	  buster.assert(3 == stat);		// CLOSED

	  server.stop("alpha", function (e) {
	    buster.refute(e);
	    done();
	  });
	});
    },

  'ledger_current' :
    function (done) {
      alpha.request_ledger_current(function (r) {
	  console.log(r);

	  buster.assert.equals(r.ledger_current_index, 3);
	  done();
	});
    },

  '// ledger_closed' :
    function (done) {
      alpha.request_ledger_closed(function (r) {
	  console.log("result: %s", JSON.stringify(r));

	  buster.assert.equals(r.ledger_closed_index, 2);
	  done();
	});
    },

  'account_root success' :
    function (done) {
      alpha.request_ledger_closed(function (r) {
	  // console.log("result: %s", JSON.stringify(r));

	  buster.refute(r.error);

	  alpha.request_ledger_entry({
	      'ledger_closed' : r.ledger_closed,
	      'type' : 'account_root',
	      'account_root' : 'iHb9CJAWyB4ij91VRWn96DkukG4bwdtyTh'
	    } , function (r) {
	      // console.log("account_root: %s", JSON.stringify(r));

	      buster.assert('node' in r);
	      done();
	    });
	});
    },

  'account_root malformedAddress' :
    function (done) {
      alpha.request_ledger_closed(function (r) {
	  // console.log("result: %s", JSON.stringify(r));

	  buster.refute(r.error);

	  alpha.request_ledger_entry({
	      'ledger_closed' : r.ledger_closed,
	      'type' : 'account_root',
	      'account_root' : 'foobar'
	    } , function (r) {
	      // console.log("account_root: %s", JSON.stringify(r));

	      buster.assert.equals(r.error, 'malformedAddress');
	      done();
	    });
	});
    },

  'account_root entryNotFound' :
    function (done) {
      alpha.request_ledger_closed(function (r) {
	  console.log("result: %s", JSON.stringify(r));

	  buster.refute(r.error);

	  alpha.request_ledger_entry({
	      'ledger_closed' : r.ledger_closed,
	      'type' : 'account_root',
	      'account_root' : 'iG1QQv2nh2gi7RCZ1P8YYcBUKCCN633jCn'
	    }, function (r) {
	      console.log("account_root: %s", JSON.stringify(r));

	      buster.assert.equals(r.error, 'entryNotFound');
	      done();
	    });
	});
    },

  'ledger_entry index' :
    function (done) {
      alpha.request_ledger_closed(function (r) {
	  // console.log("result: %s", JSON.stringify(r));

	  buster.refute(r.error);

	  alpha.request_ledger_entry({
	      'ledger_closed' : r.ledger_closed,
	      'type' : 'account_root',
	      'index' : "2B6AC232AA4C4BE41BF49D2459FA4A0347E1B543A4C92FCEE0821C0201E2E9A8",
	    } , function (r) {
	      console.log("node: %s", JSON.stringify(r));

	      buster.assert('node_binary' in r);
	      done();
	    });
	});
    },
});

// vim:sw=2:sts=2:ts=8
