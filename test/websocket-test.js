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
      var alpha	= remote.remoteConfig(config, "alpha", 'TRACE');

      alpha
	.on('connected', function () {
	    // OPEN
	    buster.assert(true);

	    alpha
	      .on('disconnected', function () {
		  // CLOSED
		  buster.assert(true);
		  done();
		})
	      .connect(false);
	  })
	.connect();
    },
});

// vim:sw=2:sts=2:ts=8
