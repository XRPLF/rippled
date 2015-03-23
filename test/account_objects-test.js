/* -------------------------------- REQUIRES -------------------------------- */

var async        = require('async');
var assert       = require('assert-diff');
var lodash       = require('lodash');

var Remote       = require('ripple-lib').Remote;
var Request      = require('ripple-lib').Request;
var Account      = require('ripple-lib').UInt160;
var testutils    = require('./testutils');
var LedgerState  = require('./ledger-state').LedgerState;

var config       = testutils.init_config();
// We just use equal instead of strictEqual everywhere.
assert.options.strict = true;

/* --------------------------------- HELPERS -------------------------------- */

function noop() {}

function account_objects(remote, account, params, callback) {
  if (lodash.isFunction(params)) {
    callback = params;
    params = null;
  }

  var request = new Request(remote, 'account_objects');
  request.message.account = Account.json_rewrite(account);
  lodash.forOwn(params || {}, function(v, k) { request.message[k] = v; });

  request.callback(callback);
}

function filter_threading_fields(entries) {
  return entries.map(function(entry) {
    return lodash.omit(entry, ['PreviousTxnID', 'PreviousTxnLgrSeq']);
  });
}

/* ---------------------------------- TEST ---------------------------------- */

suite('account_objects', function() {
  // A declarative description of the ledger
  var ledger_state = {
    accounts: {
      // Gateways
      G1 : {balance: ["1000.0"]},
      G2 : {balance: ["1000.0"]},

      // Bob has two RippleState and two Offer account objects.
      bob : {
        balance: ["1000.0", "1000/USD/G1",
                            "1000/USD/G2"],
        // these offers will be in `Sequence`
        offers: [["100.0", "1/USD/bob"],
                 ["100.0", "1/USD/G1"]]
      }
    }
  };

  // build_(setup|teardown) utils functions set state on this context var.
  var context = {};

  // After setup we bind the remote to `account_objects` helper above.
  var request_account_objects;
  var bob;
  var G1;
  var G2;

  // This runs only once
  suiteSetup(function(done) {
    testutils.build_setup().call(context, function() {
      request_account_objects = account_objects.bind(null, context.remote);
      var ledger = new LedgerState(ledger_state,
                                   assert, context.remote,
                                   config);

      // Get references to the account objects for usage later.
      bob = Account.json_rewrite('bob');
      G1 = Account.json_rewrite('G1');
      G2 = Account.json_rewrite('G2');

      // Run the ledger setup util. This compiles the declarative description
      // into a series of transactions and executes them.
      ledger.setup(noop /*logger*/, function(){
        done();
      })
    });
  });

  suiteTeardown(function(done) {
    testutils.build_teardown().call(context, done);
  });

  // With PreviousTxnID, PreviousTxnLgrSeq omitted.
  var bobs_objects = [
    {
      "Balance": {
        "currency": "USD",
        "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
        "value": "-1000"
      },
      "Flags": 131072,
      "HighLimit": {
        "currency": "USD",
        "issuer": "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
        "value": "1000"
      },
      "HighNode": "0000000000000000",
      "LedgerEntryType": "RippleState",
      "LowLimit": {
        "currency": "USD",
        "issuer": "r32rQHyesiTtdWFU7UJVtff4nCR5SHCbJW",
        "value": "0"
      },
      "LowNode": "0000000000000000",
      "index":
        "D89BC239086183EB9458C396E643795C1134963E6550E682A190A5F021766D43"
    },
    {
      "Balance": {
        "currency": "USD",
        "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
        "value": "-1000"
      },
      "Flags": 131072,
      "HighLimit": {
        "currency": "USD",
        "issuer": "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
        "value": "1000"
      },
      "HighNode": "0000000000000000",
      "LedgerEntryType": "RippleState",
      "LowLimit": {
        "currency": "USD",
        "issuer": "r9cZvwKU3zzuZK9JFovGg1JC5n7QiqNL8L",
        "value": "0"
      },
      "LowNode": "0000000000000000",
      "index":
        "D13183BCFFC9AAC9F96AEBB5F66E4A652AD1F5D10273AEB615478302BEBFD4A4"
    },
    {
      "Account": "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
      "BookDirectory":
        "50AD0A9E54D2B381288D535EB724E4275FFBF41580D28A925D038D7EA4C68000",
      "BookNode": "0000000000000000",
      "Flags": 65536,
      "LedgerEntryType": "Offer",
      "OwnerNode": "0000000000000000",
      "Sequence": 4,
      "TakerGets": {
        "currency": "USD",
        "issuer": "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
        "value": "1"
      },
      "TakerPays": "100000000",
      "index":
        "A984D036A0E562433A8377CA57D1A1E056E58C0D04818F8DFD3A1AA3F217DD82"
    },
    {
      "Account": "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
      "BookDirectory":
        "B025997A323F5C3E03DDF1334471F5984ABDE31C59D463525D038D7EA4C68000",
      "BookNode": "0000000000000000",
      "Flags": 65536,
      "LedgerEntryType": "Offer",
      "OwnerNode": "0000000000000000",
      "Sequence": 5,
      "TakerGets": {
        "currency": "USD",
        "issuer": "r32rQHyesiTtdWFU7UJVtff4nCR5SHCbJW",
        "value": "1"
      },
      "TakerPays": "100000000",
      "index":
        "CAFE32332D752387B01083B60CC63069BA4A969C9730836929F841450F6A718E"
    }
  ]

  test('stepped 1 at a time using marker/limit', function(done) {

    // We step through bob's account objects one at a time by using `limit` and
    // `marker` and for each object we see, we `push` them onto this array so we
    // can later  check it against an un`limit`ed request.
    var objects_stepped = [];

    var steps = [
      function first_ripple_state(next) {
        request_account_objects('bob', {limit: 1}, function(e, m) {
          assert.ifError(e);

          var objects = m.account_objects;
          var ripple_state = m.account_objects[0];

          assert.equal(m.limit, 1);
          assert.equal(objects.length, 1);
          assert.equal(ripple_state.LedgerEntryType, 'RippleState');
          assert.equal(ripple_state.HighLimit.issuer, bob);
          assert.equal(ripple_state.LowLimit.issuer, G1);

          objects_stepped.push(ripple_state);
          next(null, m.marker);
        });
      },

      function second_ripple_state(resume_marker, next) {
        request_account_objects('bob', {limit: 1, marker: resume_marker},
                                                          function(e, m) {
          assert.ifError(e);

          var objects = m.account_objects;
          var ripple_state = m.account_objects[0];


          assert.equal(m.limit, 1);
          assert.equal(objects.length, 1);
          assert.equal(ripple_state.LedgerEntryType, 'RippleState');
          assert.equal(ripple_state.HighLimit.issuer, bob);
          assert.equal(ripple_state.LowLimit.issuer, G2);

          objects_stepped.push(ripple_state);
          next(null, m.marker);
        });
      },

      function first_offer(resume_marker, next) {
        request_account_objects('bob', {limit: 1, marker: resume_marker},
                                                          function(e, m) {
          assert.ifError(e);

          var objects = m.account_objects;
          var offer = m.account_objects[0];

          assert.equal(m.limit, 1);
          assert.equal(objects.length, 1);

          assert.equal(offer.LedgerEntryType, 'Offer');
          assert.equal(offer.TakerGets.issuer, bob);
          assert.equal(offer.Account, bob);

          objects_stepped.push(offer);
          next(null, m.marker);
        });
      },

      function second_offer(resume_marker, next) {
        request_account_objects('bob', {limit: 1, marker: resume_marker},
                                                          function(e, m) {
          assert.ifError(e);

          var objects = m.account_objects;
          var offer = m.account_objects[0];

          assert.equal(objects.length, 1);

          assert.equal(offer.Account, bob);
          assert.equal(offer.LedgerEntryType, 'Offer');
          assert.equal(offer.TakerGets.issuer, G1);

          assert.equal(m.marker, undefined);
          objects_stepped.push(offer);
          next();
        });
      },
    ];

    async.waterfall(steps, function (err, result) {
      assert.ifError(err);

      var filtered = filter_threading_fields(objects_stepped);
      // Compare against a known/inspected exchaustive response.
      assert.deepEqual(filtered, bobs_objects);
      done();
    });
  });

  test('unstepped', function(done) {
    request_account_objects('bob', function(e, m){
      var objects = m.account_objects;
      assert.equal(m.marker, undefined);

      var filtered = filter_threading_fields(objects);
      // Compare against a known/inspected exchaustive response.
      assert.deepEqual(filtered, bobs_objects);
      done();
    });
  });
});
