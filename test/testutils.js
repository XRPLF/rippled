var async       = require('async');
var assert      = require('assert');
var extend      = require('extend');
var Amount      = require('ripple-lib').Amount;
var Remote      = require('ripple-lib').Remote;
var Transaction = require('ripple-lib').Transaction;
var Server      = require('./server').Server;
var server      = { };

function get_config() {
  var cfg = require(__dirname + '/config-example');

  // See if the person testing wants to override the configuration by creating a
  // file called test/config.js.
  try {
    cfg = extend({}, cfg, require(__dirname + '/config'));
  } catch (e) { }

  return cfg;
};

function init_config() {
  return require('ripple-lib').config.load(get_config());
};

function prepare_tests(tests, fn) {
  var tests = typeof tests === 'string' ? [ tests ] : tests;
  var result = [ ];
  for (var i in tests) {
    result.push(fn(tests[i], i));
  }
  return result;
};

function account_dump(remote, account, callback) {
  var self = this;

  this.what = 'Get latest account_root';

  var request = remote.request_ledger_entry('account_root');
  request.ledger_hash(remote.ledger_hash());
  request.account_root('root');
  request.callback(function(err, r) {
    assert(!err, self.what);
    callback(err);
  });

  // get closed ledger hash
  // get account root
  // construct a json result
};

/**
 * Helper called by test cases to generate a setUp routine.
 *
 * By default you would call this without options, but it is useful to
 * be able to plug options in during development for quick and easy
 * debugging.
 *
 * @example
 *   buster.testCase('Foobar', {
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
function build_setup(opts, host) {
  var config = get_config();
  var host = host || config.server_default;
  var opts = opts || {};

  // Normalize options
  if (opts.verbose) {
    opts.verbose_ws     = true;
    opts.verbose_server = true;
  };

  function setup(done) {
    var self = this;

    self.compute_fees_amount_for_txs = function(txs) {
      var fee_units = Transaction.fee_units['default'] * txs;
      return self.remote.fee_tx(fee_units);
    };

    self.amount_for = function(options) {
      var reserve = self.remote.reserve(options.ledger_entries || 0);
      var fees = self.compute_fees_amount_for_txs(options.default_transactions || 0)
      return reserve.add(fees).add(options.extra || 0);
    };

    this.store = this.store || {};
    this.store[host] = this.store[host] || { };
    var data = this.store[host];

    data.opts = opts;

    var series = [
      function run_server(callback) {
        if (opts.no_server)  {
          return callback();
        }

        var server_config = extend({}, config.default_server_config, config.servers[host]);

        data.server = Server.from_config(host, server_config, !!opts.verbose_server);

        data.server.once('started', function() {
          callback();
        });

        data.server.once('exited', function () {
          // If know the remote, tell it server is gone.
          if (self.remote) {
            self.remote.server_fatal();
          }
        });

        server[host] = data.server;
        data.server.start();
      },

      function connect_websocket(callback) {
        self.remote = data.remote = Remote.from_config(host, !!opts.verbose_ws);
        self.remote.connect(callback);
      },

      function subscribe_account(callback) {
        var root = self.remote.account('root');
        self.remote.request_subscribe().accounts(root._account_id).callback(callback);
      },

      function create_ledger_interval(callback) {
        if (opts.no_auto_ledger_close) {return callback(); };

        self.ledger_interval = setInterval(function() {
          self.remote.ledger_accept();
        }, 200);  

        callback();
      }

    ];

    async.series(series, done);
  };

  return setup;
};

/**
 * Generate tearDown routine.
 *
 * @param host {String} Identifier for the host configuration to be used.
 */
function build_teardown(host) {
  var config = get_config();
  var host = host || config.server_default;

  function teardown(done) {
    var self = this;
    var data = this.store[host];
    var opts = data.opts;

    var series = [
      function clear_ledger_interval(callback) {
        if (self.ledger_interval != null) {clearInterval(self.ledger_interval)};
        callback();
      },

      function disconnect_websocket(callback) {
        data.remote.once('disconnected', callback)
        data.remote.disconnect();
      },

      function stop_server(callback) {
        if (opts.no_server) {
          callback();
        } else {
          data.server.once('stopped', callback)
          data.server.stop();
          delete server[host];
        }
      }
    ];

    async.series(series, done);
  };

  return teardown;
};

function create_accounts(remote, src, amount, accounts, callback) {
  assert.strictEqual(arguments.length, 5);

  remote.set_account_seq(src, 1);

  async.forEach(accounts, function (account, nextAccount) {
    // Cache the seq as 1.
    // Otherwise, when other operations attempt to opperate async against the account they may get confused.

    remote.set_account_seq(account, 1);

    var tx = remote.transaction();
    tx.payment(src, account, amount);
    tx.submit(nextAccount);

  }, callback);
};

function credit_limit(remote, src, amount, callback) {
  assert.strictEqual(arguments.length, 4);

  var _m = amount.match(/^(\d+\/...\/[^\:]+)(?::(\d+)(?:,(\d+))?)?$/);

  if (!_m) {
    //console.log('credit_limit: parse error: %s', amount);
    return callback(new Error('parse_error'));
  }

  // console.log('credit_limit: parsed: %s', JSON.stringify(_m, undefined, 2));
  var account_limit = _m[1];
  var quality_in    = _m[2];
  var quality_out   = _m[3];

  var tx = remote.transaction();

  tx.ripple_line_set(src, account_limit, quality_in, quality_out);

  tx.submit(function(err,  m) {
    //console.log('proposed: %s', JSON.stringify(m));
    if (err) {
      callback(err);
    } else if (m.engine_result === 'tesSUCCESS') {
      callback();
    } else {
      callback(m);
    }
  });
};

function verify_limit(remote, src, amount, callback) {
  assert.strictEqual(arguments.length, 4);

  var _m = amount.match(/^(\d+\/...\/[^\:]+)(?::(\d+)(?:,(\d+))?)?$/);

  if (!_m) {
    // console.log('credit_limit: parse error: %s', amount);
    return callback(new Error('parse_error'));
  }

  // console.log('_m', _m.length, _m);
  // console.log('verify_limit: parsed: %s', JSON.stringify(_m, undefined, 2));
  var account_limit = _m[1];
  var quality_in    = Number(_m[2]);
  var quality_out   = Number(_m[3]);
  var limit         = Amount.from_json(account_limit);

  var options = {
    account:   src,
    issuer:    limit.issuer().to_json(),
    currency:  limit.currency().to_json(),
    ledger:    'CURRENT'
  };

  remote.request_ripple_balance(options, function(err, m) {
    if (err) {
      callback(err);
    } else {
      assert(m.account_limit.equals(limit));
      assert(isNaN(quality_in) || m.account_quality_in === quality_in);
      assert(isNaN(quality_out) || m.account_quality_out === quality_out);
      callback();
    }
  });
};

function credit_limits(remote, balances, callback) {
  assert.strictEqual(arguments.length, 3);

  var limits = [ ];

  for (var src in balances) {
    prepare_tests(balances[src], function(amount) {
      limits.push({
        source: src,
        amount: amount
      });
    });
  }

  function iterator(limit, callback) {
    credit_limit(remote, limit.source, limit.amount, callback);
  };

  async.some(limits, iterator, callback);
};

function ledger_close(remote, callback) {
  remote.once('ledger_closed', function (m) {
    callback();
  });
  remote.ledger_accept();
};

function payment(remote, src, dst, amount, callback) {
  assert.strictEqual(arguments.length, 5);

  var tx = remote.transaction();

  tx.payment(src, dst, amount);

  tx.submit(function(err, m) {
    // console.log('proposed: %s', JSON.stringify(m));
    if (err) {
      callback(err);
    } else if (m.engine_result === 'tesSUCCESS') {
      callback();
    } else {
      callback(m);
    }
  });
};

function payments(remote, balances, callback) {
  assert.strictEqual(arguments.length, 3);

  var sends = [ ];

  for (var src in balances) {
    prepare_tests(balances[src], function(amount_json) {
      sends.push({
        source:        src,
        destination :  Amount.from_json(amount_json).issuer().to_json(),
        amount :       amount_json
      });
    });
  }

  function iterator(send, callback) {
    payment(remote, send.source, send.destination, send.amount, callback);
  };

  async.some(sends, iterator, callback);
};

function transfer_rate(remote, src, billionths, callback) {
  assert.strictEqual(arguments.length, 4);

  var tx = remote.transaction();
  tx.account_set(src);
  tx.transfer_rate(billionths);
  tx.submit(callback);
};

function verify_balance(remote, src, amount_json, callback) {
  assert.strictEqual(arguments.length, 4);

  var amount_req  = Amount.from_json(amount_json);

  if (amount_req.is_native()) {
    remote.request_account_balance(src, 'CURRENT', function(err, amount_act) {
      assert.ifError(err);
      assert.strictEqual(amount_act.to_json(), amount_req.to_json());
      callback();
    });
  } else {
    var issuer = amount_req.issuer().to_json();
    var currency = amount_req.currency().to_json();
    remote.request_ripple_balance(src, issuer, currency, 'CURRENT', function(err, m) {
      if (err) return callback(err);

      // console.log('BALANCE: %s', JSON.stringify(m));
      // console.log('account_balance: %s', m.account_balance.to_text_full());
      // console.log('account_limit: %s', m.account_limit.to_text_full());
      // console.log('issuer_balance: %s', m.issuer_balance.to_text_full());
      // console.log('issuer_limit: %s', m.issuer_limit.to_text_full());
      var account_balance = Amount.from_json(m.account_balance);
      var valid_balance = account_balance.equals(amount_req, true);

      if (valid_balance) {
        callback();
      } else {
        callback(new Error('Invalid balance: ' + amount_req.to_text() + '/' + Amount.from_json(account_balance).to_text()));
      }
    })
  }
};

function verify_balances(remote, balances, callback) {
  var tests = [ ];

  for (var src in balances) {
    prepare_tests(balances[src], function(amount) {
      tests.push({ source: src, amount: amount });
    });
  }

  function iterator(test, callback) {
    verify_balance(remote, test.source, test.amount, callback)
  };

  async.eachSeries(tests, iterator, callback);
};

// --> owner: account
// --> seq: sequence number of creating transaction.
// --> taker_gets: json amount
// --> taker_pays: json amount
function verify_offer(remote, owner, seq, taker_pays, taker_gets, callback) {
  assert.strictEqual(arguments.length, 6);

  var request = remote.request_ledger_entry('offer')
  request.offer_id(owner, seq)
  request.callback(function(err, m) {
    var wrong = err
    || !Amount.from_json(m.node.TakerGets).equals(Amount.from_json(taker_gets), true)
    || !Amount.from_json(m.node.TakerPays).equals(Amount.from_json(taker_pays), true);

    if (wrong) {
      //console.log('verify_offer: failed: %s', JSON.stringify(m));
      callback(err);
    } else {
      callback();
    }
  });
};

function verify_offer_not_found(remote, owner, seq, callback) {
  assert.strictEqual(arguments.length, 4);

  var request = remote.request_ledger_entry('offer');
  request.offer_id(owner, seq);
  request.callback(function(err, m) {
    if (err && err.error === 'remoteError' && err.remote.error === 'entryNotFound') {
      callback();
    } else {
      callback(new Error('Expected remoteError entryNotFound: ' + m));
    }
  });
};

function verify_owner_count(remote, account, count, callback) {
  assert.strictEqual(arguments.length, 4);

  var options = { account: account, ledger: 'CURRENT' };

  remote.request_owner_count(options, function(err, owner_count) {
    //console.log('owner_count: %s/%d', owner_count, value);
    if (err) {
      callback(err);
    } else if (owner_count !== count) {
      callback(new Error('Owner count mismatch: ' + owner_count + '/' + count));
    } else {
      callback();
    }
  });
};

function verify_owner_counts(remote, counts, callback) {
  var tests = prepare_tests(counts, function(account) {
    return { account: account, count: counts[account] };
  });

  function iterator(test, callback) {
    verify_owner_count(remote, test.account, test.count, callback)
  };

  async.eachSeries(tests, iterator, function(err) {
    if (err) {
      callback();
    } else {
      callback(new Error('Owner counts mismatch'));
    }
  });
};

exports.account_dump            = account_dump;
exports.build_setup             = build_setup;
exports.build_teardown          = build_teardown;
exports.create_accounts         = create_accounts;
exports.credit_limit            = credit_limit;
exports.credit_limits           = credit_limits;
exports.get_config              = get_config;
exports.init_config             = init_config;
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

process.on('uncaughtException', function() {
  Object.keys(server).forEach(function(host) {
    server[host].stop();
  });
});

// vim:sw=2:sts=2:ts=8:et
