var buster  = require("buster");

var server  = require("./server.js");

// How long to wait for server to start.
var serverDelay = 1500;

buster.testRunner.timeout = 5000;

buster.testCase("Standalone server startup", {
  "server start and stop" : function (done) {
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

// vim:sw=2:sts=2:ts=8
