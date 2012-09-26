// console.log("standalone-test.js>");

var fs = require("fs");
var buster = require("buster");

var server = require("./server.js");
var remote = require("../js/remote.js");
var config = require("./config.js");

// How long to wait for server to start.
var serverDelay = 1500;

buster.testRunner.timeout = 5000;

buster.testCase("Standalone server startup", {
    "server start and stop": function(done) {
			server.start("alpha",
				function(e) {
					buster.refute(e);
					server.stop("alpha", function(e) {
						buster.refute(e);
						done();
					});
				});
		}
});

buster.testCase("WebSocket connection", {
	'setUp' :
		function(done) {
			server.start("alpha",
				function(e) {
					buster.refute(e);
					done();
				}
			);
		},

	'tearDown' :
		function(done) {
			server.stop("alpha", function(e) {
				buster.refute(e);
				done();
			});
		},

    "websocket connect and disconnect" :
		function(done) {
			var alpha	= remote.remoteConfig(config, "alpha");

			alpha.connect(function(stat) {
				buster.assert(1 == stat);				// OPEN

				alpha.disconnect(function(stat) {
						buster.assert(3 == stat);		// CLOSED
						done();
					});
				}, serverDelay);
		},
});

// XXX Figure out a way to stuff this into the test case.
var alpha;

buster.testCase("Websocket commands", {
	'setUp' :
		function(done) {
			server.start("alpha",
				function(e) {
					buster.refute(e);

					alpha	= remote.remoteConfig(config, "alpha");

					alpha.connect(function(stat) {
							buster.assert(1 == stat);	// OPEN

							done();
						}, serverDelay);
			});
		},

	'tearDown' :
		function(done) {
			alpha.disconnect(function(stat) {
					buster.assert(3 == stat);			// CLOSED

					server.stop("alpha", function(e) {
						buster.refute(e);

						done();
					});
				});
		},

	"ledger_current" :
		function(done) {
			alpha.ledger_current(function (r) {
					console.log(r);

					buster.assert(r.ledger === 2);
					done();
				});
		}
});

// vim:ts=4
