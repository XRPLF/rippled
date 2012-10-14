var buster  = require("buster");

var config  = require("./config.js");
var server  = require("./server.js");
var remote  = require("../js/remote.js");

// How long to wait for server to start.
var serverDelay = 1500;

buster.testRunner.timeout = 5000;

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
