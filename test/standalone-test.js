// console.log("standalone-test.js>");

var fs = require("fs");
var buster = require("buster");

var server = require("./server.js");

buster.testCase("Check standalone server startup", {
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

// console.log("standalone-test.js<");
// vim:ts=4
