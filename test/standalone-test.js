// console.log("standalone-test.js>");

var fs = require("fs");
var buster = require("buster");

var server = require("./server.js");

buster.testCase("Check standalone server startup", {
    "Start": function (done) {
			server.start("alpha", function(e) {
					buster.refute(e);
					done();
				});
		}
});

// console.log("standalone-test.js<");
// vim:ts=4
