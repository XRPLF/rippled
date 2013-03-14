var async   = require("async");

var Amount  = require("../src/js/amount").Amount;
var Remote  = require("../src/js/remote").Remote;
var Server  = require("./server").Server;

var config  = require('../src/js/config').load(require('./config'));

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

        data.server = Server
                        .from_config(host, !!opts.verbose_server)
                        .on('started', callback)
                        .on('exited', function () {
                            // If know the remote, tell it server is gone.
                            if (self.remote)
                              self.remote.server_fatal();
                          })
                        .start();
      },
      function connectWebsocketStep(callback) {
        self.remote = data.remote =
          Remote
            .from_config(host, !!opts.verbose_ws)
            .once('ledger_closed', callback)
            .connect();
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

  remote.set_account_seq(src, 1);

  async.forEach(accounts, function (account, callback) {
    // Cache the seq as 1.
    // Otherwise, when other operations attempt to opperate async against the account they may get confused.
    remote.set_account_seq(account, 1);

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

  var _m      = amount.match(/^(\d+\/...\/[^\:]+)(?::(\d+)(?:,(\d+))?)?$/);
  if (!_m) {
    console.log("credit_limit: parse error: %s", amount);

    callback('parse_error');
  }
  else
  {
    // console.log("credit_limit: parsed: %s", JSON.stringify(_m, undefined, 2));
    var _account_limit  = _m[1];
    var _quality_in     = _m[2];
    var _quality_out    = _m[3];

    remote.transaction()
      .ripple_line_set(src, _account_limit, _quality_in, _quality_out)
      .on('proposed', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));

          callback(m.result != 'tesSUCCESS');
        })
      .on('error', function (m) {
          // console.log("error: %s", JSON.stringify(m));

          callback(m);
        })
      .submit();
  }
};

var verify_limit = function (remote, src, amount, callback) {
  assert(4 === arguments.length);

  var _m      = amount.match(/^(\d+\/...\/[^\:]+)(?::(\d+)(?:,(\d+))?)?$/);
  if (!_m) {
    // console.log("credit_limit: parse error: %s", amount);

    callback('parse_error');
  }
  else
  {
    // console.log("verify_limit: parsed: %s", JSON.stringify(_m, undefined, 2));
    var _account_limit  = _m[1];
    var _quality_in     = _m[2];
    var _quality_out    = _m[3];

    var _limit          = Amount.from_json(_account_limit);

    remote.request_ripple_balance(src, _limit.issuer().to_json(), _limit.currency().to_json(), 'CURRENT')
      .once('ripple_state', function (m) {
          buster.assert(m.account_limit.equals(_limit));
          buster.assert('undefined' === _quality_in || m.account_quality_in == _quality_in);
          buster.assert('undefined' === _quality_out || m.account_quality_out == _quality_out);

          callback();
        })
      .on('error', function (m) {
          // console.log("error: %s", JSON.stringify(m));

          callback(m);
        })
      .request();
  }
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

var ledger_close = function (remote, callback) {
  remote.once('ledger_closed', function (m) { callback(); }).ledger_accept();
}

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
          if (!amount_act.equals(amount_req, true)) {
            console.log("verify_balance: failed: %s / %s",
              amount_act.to_text_full(),
              amount_req.to_text_full());
          }

          callback(!amount_act.equals(amount_req, true));
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

          if (!account_balance.equals(amount_req, true)) {
            console.log("verify_balance: failed: %s vs %s / %s: %s",
                        src,
                        account_balance.to_text_full(),
                        amount_req.to_text_full(),
                        account_balance.not_equals_why(amount_req, true));
          }

          callback(!account_balance.equals(amount_req, true));
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
        var wrong = !Amount.from_json(m.node.TakerGets).equals(Amount.from_json(taker_gets), true)
          || !Amount.from_json(m.node.TakerPays).equals(Amount.from_json(taker_pays), true);

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

var verify_owner_count = function (remote, account, value, callback) {
  assert(4 === arguments.length);

  remote.request_owner_count(account, 'CURRENT')
    .once('owner_count', function (owner_count) {
        if (owner_count !== value)
          console.log("owner_count: %s/%d", owner_count, value);

        callback(owner_count !== value);
      })
    .request();
};

var verify_owner_counts = function (remote, counts, callback) {
  var tests = [];

  for (var src in counts) {
      tests.push( { "source" : src, "count" : counts[src] } );
  }

  async.every(tests,
    function (check, callback) {
      verify_owner_count(remote, check.source, check.count,
        function (mismatch) { callback(!mismatch); });
    },
    function (every) {
      callback(!every);
    });
};

exports.account_dump            = account_dump;
exports.build_setup             = build_setup;
exports.build_teardown          = build_teardown;
exports.create_accounts         = create_accounts;
exports.credit_limit            = credit_limit;
exports.credit_limits           = credit_limits;
exports.ledger_close            = ledger_close;
exports.payment                 = payment;
exports.payments                = payments;
exports.transfer_rate           = transfer_rate;
exports.verify_balance          = verify_balance;
exports.verify_balances         = verify_balances;
exports.verify_limit            = verify_limit;
exports.verify_offer            = verify_offer;
exports.verify_offer_not_found  = verify_offer_not_found;
exports.verify_owner_count      = verify_owner_count;
exports.verify_owner_counts     = verify_owner_counts;

// vim:sw=2:sts=2:ts=8:et
