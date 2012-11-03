var async   = require("async");
// var buster  = require("buster");

var Amount  = require("../js/amount.js").Amount;
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
 * @param opts {Object} These options allow quick-and-dirty test-specific
 *   customizations of your test environment.
 * @param opts.verbose {Bool} Enable all debug output (then cover your ears
 *   and run)
 * @param opts.verbose_ws {Bool} Enable tracing in the Remote class. Prints
 *   websocket traffic.
 * @param opts.verbose_server {Bool} Set the -v option when running rippled.
 * @param opts.no_server {Bool} Don't auto-run rippled.
 * @param host {String} Identifier for the host configuration to be used.
 */
var build_setup = function (opts, host) {
  opts = opts || {};

  // Normalize options
  if (opts.verbose) {
    opts.verbose_ws = true;
    opts.verbose_server = true;
  };

  return function (done) {
    var self = this;

    host = host || config.server_default;

    this.store = this.store || {};

    var data = this.store[host] = this.store[host] || {};

    data.opts = opts;

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

/**
 * Generate tearDown routine.
 *
 * @param host {String} Identifier for the host configuration to be used.
 */
var build_teardown = function (host) {
  return function (done) {
    host = host || config.server_default;

    var data = this.store[host];
    var opts = data.opts;

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

var credit_limits = function (remote, balances, callback) {
  assert(3 === arguments.length);

  var limits = [];

  for (var src in balances) {
    var	values_src  = balances[src];
    var values	    = 'string' === typeof values_src ? [ values_src ] : values_src;

    for (var index in values) {
      limits.push( { "source" : src, "amount" : values[index] } );
    }
  }

  async.every(limits,
    function (limit, callback) {
      credit_limit(remote, limit.source, limit.amount,
	function (mismatch) { callback(!mismatch); });
    },
    function (every) {
      callback(!every);
    });
};

var payment = function (remote, src, dst, amount, callback) {
  assert(5 === arguments.length);

  remote.transaction()
    .payment(src, dst, amount)
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

var payments = function (remote, balances, callback) {
  assert(3 === arguments.length);

  var sends = [];

  for (var src in balances) {
    var	values_src  = balances[src];
    var values	    = 'string' === typeof values_src ? [ values_src ] : values_src;

    for (var index in values) {
      var amount_json = values[index];
      var amount      = Amount.from_json(amount_json);

      sends.push( { "source" : src, "destination" : amount.issuer.to_json(), "amount" : amount_json } );
    }
  }

  async.every(sends,
    function (send, callback) {
      payment(remote, send.source, send.destination, send.amount,
	function (mismatch) { callback(!mismatch); });
    },
    function (every) {
      callback(!every);
    });
};

var transfer_rate = function (remote, src, billionths, callback) {
  assert(4 === arguments.length);

  remote.transaction()
    .account_set(src)
    .transfer_rate(billionths)
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

var verify_balance = function (remote, src, amount_json, callback) {
  assert(4 === arguments.length);
  var amount  = Amount.from_json(amount_json);

  remote.request_ripple_balance(src, amount.issuer.to_json(), amount.currency.to_json(), 'CURRENT')
    .once('ripple_state', function (m) {
//	console.log("BALANCE: %s", JSON.stringify(m));
//	console.log("account_balance: %s", m.account_balance.to_text_full());
//	console.log("account_limit: %s", m.account_limit.to_text_full());
//	console.log("issuer_balance: %s", m.issuer_balance.to_text_full());
//	console.log("issuer_limit: %s", m.issuer_limit.to_text_full());

	if (!m.account_balance.equals(amount)) {
	  console.log("verify_balance: failed: %s vs %s is %s", src, amount_json, amount.to_text_full());
	}

	callback(!m.account_balance.equals(amount));
      })
    .request();
};

var verify_balances = function (remote, balances, callback) {
  var tests = [];

  for (var src in balances) {
    var	values_src  = balances[src];
    var values	    = 'string' === typeof values_src ? [ values_src ] : values_src;

    for (var index in values) {
      tests.push( { "source" : src, "amount" : values[index] } );
    }
  }

  async.every(tests,
    function (check, callback) {
      verify_balance(remote, check.source, check.amount,
	function (mismatch) { callback(!mismatch); });
    },
    function (every) {
      callback(!every);
    });
};

exports.build_setup	    = build_setup;
exports.create_accounts	    = create_accounts;
exports.credit_limit	    = credit_limit;
exports.credit_limits	    = credit_limits;
exports.payment		    = payment;
exports.payments	    = payments;
exports.build_teardown	    = build_teardown;
exports.transfer_rate	    = transfer_rate;
exports.verify_balance	    = verify_balance;
exports.verify_balances	    = verify_balances;

// vim:sw=2:sts=2:ts=8
