var async   = require("async");
// var buster  = require("buster");

var Remote  = require("../js/remote.js").Remote;
var Server  = require("./server.js").Server;

var config  = require("./config.js");

/**
 * Helper called by test cases to generate a setUp routine.
 *
 * By default you would call this without options, but it is useful to
 * be able to plug options in during development for quick and easy
 * debugging.
 *
 * @example
 *   buster.testCase("Foobar", {
 *     setUp: testutils.build_setup({verbose: true}),
 *     // ...
 *   });
 *
 * @param host {String} Identifier for the host configuration to be used.
 * @param opts {Object} These options allow quick-and-dirty test-specific
 *   customizations of your test environment.
 * @param opts.verbose {Bool} Enable all debug output (then cover your ears
 *   and run)
 * @param opts.verbose_ws {Bool} Enable tracing in the Remote class. Prints
 *   websocket traffic.
 * @param opts.verbose_server {Bool} Set the -v option when running rippled.
 * @param opts.no_server {Bool} Don't auto-run rippled.
 */
var build_setup = function (opts) {
  opts = this.opts = opts || {};

  // Normalize options
  if (opts.verbose) {
    opts.verbose_ws = true;
    opts.verbose_server = true;
  };

  return function (done) {
    var self = this;

    var host = host || config.server_default;

    this.store = this.store || {};

    var data = this.store[host] = this.store[host] || {};

    async.series([
      function runServerStep(callback) {
        if (opts.no_server) return callback();

        data.server = Server.from_config(host, !!opts.verbose_server).on('started', callback).start();
      },
      function connectWebsocketStep(callback) {
        self.remote = data.remote = Remote.from_config(host, !!opts.verbose_ws).once('ledger_closed', callback).connect();
      }
    ], done);
  };
};

var test_teardown = function (done, host) {
  host  = host || config.server_default;

  var data = this.store[host];
  var opts = this.opts;

  async.series([
    function disconnectWebsocketStep(callback) {
       data.remote
        .on('disconnected', callback)
        .connect(false);
    },
    function stopServerStep(callback) {
      if (opts.no_server) return callback();

      data.server.on('stopped', callback).stop();
    }
  ], done);
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
	// console.log("proposed: %s", JSON.stringify(m));

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
exports.build_setup	    = build_setup;
exports.test_teardown	    = test_teardown;

// vim:sw=2:sts=2:ts=8
