var async   = require("async");

var Amount  = require("../src/js/amount.js").Amount;
var Remote  = require("../src/js/remote.js").Remote;
var Server  = require("./server.js").Server;

require("../src/js/amount.js").config = require("./config.js");
require("../src/js/remote.js").config = require("./config.js");

var config  = require("./config.js");

var account_dump = function (remote, account, callback) {
  var self = this;

  async.waterfall([
      function (callback) {
        self.what = "Get latest account_root";

        remote
          .request_ledger_entry('account_root')
          .ledger_hash(remote.ledger_hash())
          .account_root("root")
          .on('success', function (r) {
              //console.log("account_root: %s", JSON.stringify(r, undefined, 2));

              callback();
            })
          .on('error', function(m) {
              console.log("error: %s", m);

              buster.assert(false);
              callback();
            })
          .request();
      },
    ], function (error) {
      callback(error);
    });

  // get closed ledger hash
  // get account root
  // construct a json result
  //
};

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

        if (opts.no_server)
        {

                return callback();
                }

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
    var values_src  = balances[src];
    var values      = 'string' === typeof values_src ? [ values_src ] : values_src;

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
    var values_src  = balances[src];
    var values      = 'string' === typeof values_src ? [ values_src ] : values_src;

    for (var index in values) {
      var amount_json = values[index];
      var amount      = Amount.from_json(amount_json);

      sends.push( { "source" : src, "destination" : amount.issuer().to_json(), "amount" : amount_json } );
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
  var amount_req  = Amount.from_json(amount_json);

  if (amount_req.is_native()) {
    remote.request_account_balance(src, 'CURRENT')
      .once('account_balance', function (amount_act) {
          if (!amount_act.equals(amount_req))
            console.log("verify_balance: failed: %s / %s",
              amount_act.to_text_full(),
              amount_req.to_text_full());

          callback(!amount_act.equals(amount_req));
        })
      .request();
  }
  else {
    remote.request_ripple_balance(src, amount_req.issuer().to_json(), amount_req.currency().to_json(), 'CURRENT')
      .once('ripple_state', function (m) {
        // console.log("BALANCE: %s", JSON.stringify(m));
        // console.log("account_balance: %s", m.account_balance.to_text_full());
        // console.log("account_limit: %s", m.account_limit.to_text_full());
        // console.log("issuer_balance: %s", m.issuer_balance.to_text_full());
        // console.log("issuer_limit: %s", m.issuer_limit.to_text_full());

          var account_balance = Amount.from_json(m.account_balance);

          if (!account_balance.equals(amount_req)) {
            console.log("verify_balance: failed: %s vs %s is %s: %s", src, account_balance.to_text_full(), amount_req.to_text_full(), account_balance.not_equals_why(amount_req));
          }

          callback(!account_balance.equals(amount_req));
        })
      .request();
  }
};

var verify_balances = function (remote, balances, callback) {
  var tests = [];

  for (var src in balances) {
    var values_src  = balances[src];
    var values      = 'string' === typeof values_src ? [ values_src ] : values_src;

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

// --> owner: account
// --> seq: sequence number of creating transaction.
// --> taker_gets: json amount
// --> taker_pays: json amount
var verify_offer = function (remote, owner, seq, taker_pays, taker_gets, callback) {
  assert(6 === arguments.length);

  remote.request_ledger_entry('offer')
    .offer_id(owner, seq)
    .on('success', function (m) {
        var wrong   = (!Amount.from_json(m.node.TakerGets).equals(Amount.from_json(taker_gets))
          || !Amount.from_json(m.node.TakerPays).equals(Amount.from_json(taker_pays)));

        if (wrong)
          console.log("verify_offer: failed: %s", JSON.stringify(m));

        callback(wrong);
      })
    .request();
};

var verify_offer_not_found = function (remote, owner, seq, callback) {
  assert(4 === arguments.length);

  remote.request_ledger_entry('offer')
    .offer_id(owner, seq)
    .on('success', function (m) {
        console.log("verify_offer_not_found: found offer: %s", JSON.stringify(m));

        callback('entryFound');
      })
    .on('error', function (m) {
        // console.log("verify_offer_not_found: success: %s", JSON.stringify(m));

        callback('remoteError' !== m.error
          || 'entryNotFound' !== m.remote.error);
      })
    .request();
};

exports.account_dump            = account_dump;

exports.build_setup             = build_setup;
exports.create_accounts         = create_accounts;
exports.credit_limit            = credit_limit;
exports.credit_limits           = credit_limits;
exports.payment                 = payment;
exports.payments                = payments;
exports.build_teardown          = build_teardown;
exports.transfer_rate           = transfer_rate;
exports.verify_balance          = verify_balance;
exports.verify_balances         = verify_balances;
exports.verify_offer            = verify_offer;
exports.verify_offer_not_found  = verify_offer_not_found;

// vim:sw=2:sts=2:ts=8:et
