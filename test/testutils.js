var Remote  = require("../js/remote.js").Remote;
var Server  = require("./server.js").Server;

var config  = require("./config.js");

var test_setup = function (done, host) {
  var self  = this;
  var host  = host || config.server_default;

  this.store  = this.store || {};

  var data   = this.store[host] = this.store[host] || {};

  data.server = Server.from_config(host).on('started', function () {
      self.remote = data.remote = Remote.from_config(host).once('ledger_closed', done).connect();
    }).start();
};

var test_teardown = function (done, host) { 
  var host  = host || config.server_default;

  var data  = this.store[host];

  data.remote
    .on('disconnected', function () {
	data.server.on('stopped', done).stop();
      })
    .connect(false);
};

exports.test_setup = test_setup;
exports.test_teardown = test_teardown;

// vim:sw=2:sts=2:ts=8
