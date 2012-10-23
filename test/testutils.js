var async   = require("async");
// var buster  = require("buster");

var Remote  = require("../js/remote.js").Remote;
var Server  = require("./server.js").Server;

var config  = require("./config.js");

var test_setup = function (done, host, verbose) {
  var self  = this;
  var host  = host || config.server_default;

  this.store  = this.store || {};

  var data   = this.store[host] = this.store[host] || {};

  data.server = Server.from_config(host, verbose).on('started', function () {
      self.remote = data.remote = Remote.from_config(host).once('ledger_closed', done).connect();
    }).start();
};

var test_setup_verbose = function (done, host) {
  test_setup.call(this, done, host, 'VERBOSE');
}

var test_teardown = function (done, host) { 
  var host  = host || config.server_default;

  var data  = this.store[host];

  data.remote
    .on('disconnected', function () {
	data.server.on('stopped', done).stop();
      })
    .connect(false);
};

var create_accounts = function (remote, src, amount, accounts, callback) {
  assert(5 === arguments.length);

  async.forEachSeries(accounts, function (account, callback) {
    remote.transaction()
      .payment(src, account, amount)
      .set_flags('CreateAccount')
      .on('proposed', function (m) {
	  // console.log("proposed: %s", JSON.stringify(m));

	  callback(m.result != 'tesSUCCESS');
	})
      .on('error', function (m) {
	  // console.log("error: %s", JSON.stringify(m));

	  callback(m);
	})
      .submit();
    }, callback);
};

var credit_limit = function (remote, src, amount, callback) {
  assert(4 === arguments.length);

  remote.transaction()
    .ripple_line_set(src, amount)
    .on('proposed', function (m) {
	console.log("proposed: %s", JSON.stringify(m));

	// buster.assert.equals(m.result, 'tesSUCCESS');

	callback(m.result != 'tesSUCCESS');
      })
    .on('error', function (m) {
	// console.log("error: %s", JSON.stringify(m));

	callback(m);
      })
    .submit();
};

exports.create_accounts	    = create_accounts;
exports.credit_limit	    = credit_limit;
exports.test_setup	    = test_setup;
exports.test_setup_verbose  = test_setup_verbose;
exports.test_teardown	    = test_teardown;

// vim:sw=2:sts=2:ts=8
