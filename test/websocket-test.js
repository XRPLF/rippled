var buster  = require("buster");

var Server  = require("./server.js").Server;
var Remote  = require("../src/js/remote.js").Remote;
var config  = require("./config.js");

require("../src/js/remote.js").config = require("./config.js");

buster.testRunner.timeout = 5000;

buster.testCase("WebSocket connection", {
  'setUp' :
    function (done) { if (config.servers.alpha.no_server) done(); else server = Server.from_config("alpha").on('started', done).start(); },

  'tearDown' :
    function (done) { if (config.servers.alpha.no_server) done(); else server.on('stopped', done).stop();  },

  "websocket connect and disconnect" :
    function (done) {
      var alpha = Remote.from_config("alpha");

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

// vim:sw=2:sts=2:ts=8:et
