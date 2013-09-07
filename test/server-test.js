var buster    = require("buster");
var extend    = require("extend");
var Server    = require("./server").Server;

var testutils = require("./testutils");
var config    = testutils.init_config();

// How long to wait for server to start.
// var serverDelay = 1500;

var alpha;

buster.testCase("Standalone server startup", {
  "server start and stop" : function (done) {
    var cfg = extend({}, config.default_server_config,
                     config.servers.alpha);

    alpha = Server.from_config("alpha", cfg);

    alpha
      .on('started', function () {
        alpha
          .on('stopped', function () {
            buster.assert(true);

            done();
          })
          .stop();
      })
      .start();
    }
});

// vim:sw=2:sts=2:ts=8:et