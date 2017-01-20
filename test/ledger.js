var async       = require("async");
var assert      = require('assert');
var lodash      = require('lodash');
var Amount      = require("ripple-lib").Amount;
var Remote      = require("ripple-lib").Remote;
var Transaction = require("ripple-lib").Transaction;
var Server      = require("./server").Server;
var testutils   = require("./testutils");
var config      = testutils.init_config();

suite('Ledger requests', function() {
  var $ = { };

  // Array of the ledger output expected from rippled.
  // Indexes in this array represent (ledger_index - 1).
  var expectedledgers = [
    {
      "ledger": {
        "accepted": true,
        "account_hash":
          "A21ED30C04C88046FC61DB9DC19375EEDBD365FD8C17286F27127DF804E9CAA6",
        "closed": true,
        "ledger_index": "1",
        "seqNum": "1",
        "totalCoins": "100000000000000000",
        "total_coins": "100000000000000000",
        "transaction_hash":
          "0000000000000000000000000000000000000000000000000000000000000000"
      },
      "ledger_index": 1,
      "validated": true
    },
    {
      "ledger": {
        "accepted": true,
        "account_hash":
          "A21ED30C04C88046FC61DB9DC19375EEDBD365FD8C17286F27127DF804E9CAA6",
        "close_time_resolution": 30,
        "closed": true,
        "ledger_index": "2",
        "seqNum": "2",
        "totalCoins": "100000000000000000",
        "total_coins": "100000000000000000",
        "transaction_hash":
          "0000000000000000000000000000000000000000000000000000000000000000"
      },
      "ledger_index": 2,
      "validated": true
    },
    {
      "ledger": {
        "closed": false,
        "ledger_index": "3",
        "seqNum": "3"
      },
      "ledger_current_index": 3,
      "validated": false
    }
  ];

  // Indicates the ledger (as the index into
  // expectedLedgers above) that is expected
  // from rippled when it is requested by name.
  var expectedIndexByLedgerName = {
    "validated": 1,
    "closed": 1,
    "current": 2
  };

  function filterFields(ledger) {
    if (typeof ledger === 'object') {
      ledger = lodash.omit(ledger, ['close_time', 'close_time_human', 'hash', 'ledger_hash', 'parent_hash']);
      Object.keys(ledger).map(function(key) {
        ledger[key] = filterFields(ledger[key]);
      });
    }
    return ledger;
  }

  function goodLedger(ledgerSelection, self, callback) {
    self.what = "Good Ledger " + ledgerSelection;

    var request = $.remote.request_ledger(ledgerSelection)
      .on('success', function(ledger) {
        ledger = filterFields(ledger);

        var expectedIndex = expectedIndexByLedgerName[ledgerSelection];
        if (typeof expectedIndex === 'undefined')
          expectedIndex = ledgerSelection - 1;
        assert.deepEqual(ledger, expectedledgers[expectedIndex]);

        callback();
      })
      .on('error', function(ledger) {
        assert(false, self.what);
      })
      .request();
  }

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test("get ledgers by index", function(done) {
    var self = this;

    async.waterfall([
        function (callback) {
          goodLedger(1, self, callback);
        },
        function (callback) {
          goodLedger(2, self, callback);
        },
        function (callback) {
          goodLedger(3, self, callback);
        },
        function (callback) {
          self.what = "Bad Ledger (4)";

          var expected = 
            {
              "error": "remoteError",
              "error_message": "Remote reported an error.",
              "remote": {
                "error": "lgrNotFound",
                "error_code": 20,
                "error_message": "ledgerNotFound",
                "id": 4,
                "request": {
                  "command": "ledger",
                  "id": 4,
                  "ledger_index": 4
                },
                "status": "error",
                "type": "response"
              }
            };

          var request = $.remote.request_ledger(ledgerSelection)
            .on('success', function(ledger) {
              assert(false, self.what);
            })
            .on('error', function(ledger) {
              assert.deepEqual(ledger, expectedledgers[ledgerSelection - 1]);

              callback();
            })
            .request();
        },
      ],
      function(error) {
        assert(!error, self.what);
        done();
      });
  });

  test("get ledgers by name", function(done) {
    var self = this;

    async.waterfall([
        function (callback) {
          goodLedger("validated", self, callback);
        },
        function (callback) {
          goodLedger("closed", self, callback);
        },
        function (callback) {
          goodLedger("current", self, callback);
        },
      ],
      function(error) {
        assert(!error, self.what);
        done();
      });
  });
});
