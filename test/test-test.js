var async     = require("async");
var buster    = require("buster");

var Amount    = require("../src/js/amount.js").Amount;
var Remote    = require("../src/js/remote.js").Remote;
var Server    = require("./server.js").Server;


var child         = require("child_process"); //Testing spawn


var testutils = require("./testutils.js");

buster.spec.expose();

describe("My thing", function () {
    it("states the obvious", function () {
        expect(true).toBe(true);;
    });
    
    //var spawn = child.spawn,
    
    ls = child.spawn('ls', ['-lh', '/']);

	ls.stdout.on('data', function (data) {
  		console.log('stdout: ' + data);
	});

	ls.stderr.on('data', function (data) {
 		console.log('stderr: ' + data);
	});

	ls.on('exit', function (code) {
 		console.log('child process exited with code ' + code);
	});

});

/*

buster.testCase("Basic Path finding", {
  'setUp' : testutils.build_setup(),
  'tearDown' : testutils.build_teardown(),

  "two parallel paths, a -> c and a -> b -> c" :
    function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            self.what = "Create accounts.";

            testutils.create_accounts(self.remote, "root", "10000.0", ["sally","bob","rod"], callback);
          },
          function (callback) {
            self.what = "Set credit limits.";

            testutils.credit_limits(self.remote,
              {
                "rod"   : "40/USD/alice",
                "bob"	: "100/USD/alice",
                "rod"	: "37/USD/bob",
              },
              callback);
          },
          
          function (callback) {
            self.what = "Find path from alice to rod";

            self.remote.request_ripple_path_find("alice", "rod", "55/USD/rod",
              [ { 'currency' : "USD" } ])
              .on('success', function (m) {
                  // 2 alternatives.
                  buster.assert.equals(2, m.alternatives.length)
                  // Path is empty.
                  //buster.assert.equals(0, m.alternatives[0].paths_canonical.length)

                  callback();
                })
              .request();
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },
    
});

*/