var assert    = require('assert');
var extend    = require("extend");
var Server    = require("./server").Server;
var testutils = require("./testutils");
var config    = testutils.init_config();

suite('Standalone server startup', function() {
  test('server start and stop', function(done) {
    var cfg = extend({}, config.default_server_config, config.servers.alpha);
    var alpha = Server.from_config("alpha", cfg);
    alpha.on('started', function () {
      alpha.on('stopped', function () {
        done();
      })
      alpha.stop();
    })
    alpha.start();
  });
});
