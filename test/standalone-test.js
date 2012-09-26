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
				}, undefined, serverDelay);
		},
});

// var alpha	= remote.remoteConfig("alpha");
// 
// buster.testCase("Websocket commands", {
// 	'setUp' :
// 		function(done) {
// 			server.start("alpha",
// 				function(e) {
// 					buster.refute(e);
// 
// 					alpha.connect(function(stat) {
// 							buster.assert(1 == stat);	// OPEN
// 
// 							done();
// 						});
// 			});
// 		},
// 
// 	'tearDown' :
// 		function(done) {
// 			alpha.disconnect(function(stat) {
// 					buster.assert(3 == stat);			// CLOSED
// 
// 					server.stop("alpha", function(e) {
// 						buster.refute(e);
// 
// 						done();
// 					});
// 				});
// 		},
// 
// 	"assert" :
// 		function() {
// 			buster.assert(true);
// 		}
// });

// vim:ts=4
