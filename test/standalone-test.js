// console.log("standalone-test.js>");

var fs = require("fs");
var buster = require("buster");

var server = require("./server.js");
var remote = require("../js/remote.js");

buster.testCase("Check standalone server startup", {
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

buster.testCase("Check websocket connection", {
	'setUp' :
		function(done) {
			server.start("alpha",
				function(e) {
					buster.refute(e);
					done();
			});
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
			var alpha	= remote.remoteConfig("alpha");

			alpha.connect(function(stat) {
				buster.assert(1 == stat);			// OPEN

				alpha.disconnect(function(stat) {
						buster.assert(3 == stat);	// CLOSED
						done();
					});
				});
		},
});

buster.testCase("Check assert", {
	"assert" :
		function() {
			buster.assert(true);
		}
});

// vim:ts=4
