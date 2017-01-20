var assert    = require('assert');
var extend    = require("extend");
var Server    = require("./server").Server;
var testutils = require("./testutils");
var config    = testutils.init_config();

suite('Standalone server startup', function() {
  test('server start and stop', function(done) {
    var host = config.server_default;
    var cfg = testutils.get_server_config(config, host);
    var alpha = Server.from_config(host, cfg);
    alpha.on('started', function () {
      alpha.on('stopped', function () {
        done();
      })
      alpha.stop();
    })
    alpha.start();
  });
});
