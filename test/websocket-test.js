var buster    = require("buster");
var extend    = require("extend");

var Server    = require("./server").Server;
var Remote    = require("ripple-lib").Remote;

var testutils = require('./testutils');
var config    = testutils.init_config();

buster.testRunner.timeout = 5000;

var server;
buster.testCase("WebSocket connection", {
  'setUp' :
    function (done) {
      var cfg = extend({}, config.default_server_config,
                       config.servers.alpha);
      if (cfg.no_server) {
        done();
      } else {
        server = Server.from_config("alpha", cfg).on('started', done).start();
      }
    },

  'tearDown' :
    function (done) {
      if (config.servers.alpha.no_server) {
        done();
      } else {
        server.on('stopped', done).stop();
      }
    },

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
