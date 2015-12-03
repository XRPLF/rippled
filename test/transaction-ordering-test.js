let assert    = require('assert');
let _         = require('lodash');
let async     = require('async');
let testutils = require('./testutils');
let config    = testutils.init_config();
let accounts  = require('./testconfig').accounts;
let Amount    = require('ripple-lib').Amount;
let Transaction    = require('ripple-lib').Transaction;

suite('Transaction Ordering', function() {
  let $ = {};
  let opts = {};

  setup(function(done) {
    testutils.build_setup(opts).call($, done);
  });

  setup(function(done) {
    $.remote.local_signing = true;
    testutils.create_accounts(
      $.remote,
      'root',
      Amount.from_human('1000 XRP'),
      ['alice'],
      done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  function getAliceSequence() {
    return $.remote.account('alice')._entry.Sequence + 1;
  }

  function confirmAliceSeq(expected, callback) {
    let request = $.remote.requestAccountInfo(
      { account: accounts.alice.account, ledger: 'current' },
      function(err, res) {
        assert(!err);
        // console.log("Alice seq: ", res.account_data.Sequence, " Want: ", expected);
        assert.strictEqual(res.account_data.Sequence, expected);

        if(callback) {
          callback();
        }
      });
  }

  test('correct order', function(done) {
    let aliceSequence = getAliceSequence();
    confirmAliceSeq(aliceSequence);

    let tx1 = $.remote.createTransaction('AccountSet',
      {account: accounts.alice.account});
    tx1.setSequence(aliceSequence);

    let tx2 = $.remote.createTransaction('AccountSet',
      {account: accounts.alice.account});
    tx2.setSequence(aliceSequence + 1);
    tx2.setLastLedgerSequenceOffset(5);

    async.series([
      function(callback) {
        tx1.once('submitted', function(res) {
          // console.log("tx1 submitted: ", res.engine_result);
          assert.strictEqual(res.engine_result, 'tesSUCCESS');

          $.remote.ledger_accept();
        });
        tx1.once('final', function(res) {
          // console.log("tx1 final: ", res.metadata.TransactionResult);
          assert.strictEqual(res.metadata.TransactionResult,
            'tesSUCCESS');

          callback();
        });

        tx1.submit();
      },
      function(callback) {
        confirmAliceSeq(aliceSequence + 1, callback);
      },
      function(callback) {
        tx2.once('submitted', function(res) {
          // console.log("tx2 submitted: ", res.engine_result);
          assert.strictEqual(res.engine_result, 'tesSUCCESS');

          $.remote.ledger_accept();
        });
        tx2.once('final', function(res) {
          // console.log("tx2 final: ", res.metadata.TransactionResult);
          assert.strictEqual(res.metadata.TransactionResult,
            'tesSUCCESS');

          callback();
        });

        tx2.submit();
      },
      function(callback) {
        confirmAliceSeq(aliceSequence + 2, callback);
      }
      ], done);
  });

  test('incorrect order', function(done) {
    let aliceSequence = getAliceSequence();
    confirmAliceSeq(aliceSequence);

    let tx1 = $.remote.createTransaction('AccountSet',
      {account: accounts.alice.account});
    tx1.setSequence(aliceSequence);

    let tx2 = $.remote.createTransaction('AccountSet',
      {account: accounts.alice.account});
    tx2.setSequence(aliceSequence + 1);
    tx2.setLastLedgerSequenceOffset(5);

    async.series([
      function(callback) {
        // Use `on` instead of `once` for this event, because
        // we want it to fail if ripple-lib resubmits on our
        // behalf, especially if it succeeds.
        tx2.on('submitted', function(res) {
          // console.log("tx2 submitted: ", res.engine_result);
          assert.strictEqual(res.engine_result, 'terPRE_SEQ');

          callback();
        });

        tx2.submit();
      },
      function(callback) {
        confirmAliceSeq(aliceSequence, callback);
      },
      function(callback) {
        tx1.once('submitted', function(res) {
          // console.log("tx1 submitted: ", res.engine_result);
          assert.strictEqual(res.engine_result, 'tesSUCCESS');

          callback();
        });

        tx1.submit();
      },
      function(callback) {
        confirmAliceSeq(aliceSequence + 2, callback);
      },
      function(callback) {
        tx1.once('final', function(res) {
          // console.log("tx1 final: ", res.metadata.TransactionResult);
          assert.strictEqual(res.metadata.TransactionResult,
            'tesSUCCESS');
        });
        tx2.once('final', function(res) {
          // console.log("tx2 final: ", res.metadata.TransactionResult);
          assert.strictEqual(res.metadata.TransactionResult,
            'tesSUCCESS');

          callback();
        });

        $.remote.ledger_accept();
      }
      ], done);
  });

  test('incorrect order, multiple intermediaries', function(done) {
    let aliceSequence = getAliceSequence();
    confirmAliceSeq(aliceSequence);

    let tx = [];
    for(let i = 0; i < 5; ++i) {
      tx[i] = $.remote.createTransaction('AccountSet',
        {account: accounts.alice.account});
      tx[i].setSequence(aliceSequence + i);
      tx[i].setLastLedgerSequenceOffset(5);
    }

    let submits = [];
    for(let i = 0; i < 3; ++i) {
      submits = submits.concat([
        function(callback) {
          tx[i].once('submitted', function(res) {
            // console.log("tx" + i + " submitted: ", res.engine_result);
            assert.strictEqual(res.engine_result, 'tesSUCCESS');

            callback();
          });

          tx[i].submit();
        },
        function(callback) {
          confirmAliceSeq(aliceSequence + i + 1, callback);
        },
        ]);
    }

    async.series([
      function(callback) {
        // Use `on` instead of `once` for this event, because
        // we want it to fail if ripple-lib resubmits on our
        // behalf, especially if it succeeds.
        tx[4].on('submitted', function(res) {
          // console.log("tx4 submitted: ", res.engine_result);
          assert.strictEqual(res.engine_result, 'terPRE_SEQ');

          callback();
        });

        tx[4].submit();
        // Note ripple-lib has a bug/feature that it'll create
        // transactions to fill in sequence gaps if more than
        // one "ter" is received, so this is the best we can do.
      },
      function(callback) {
        confirmAliceSeq(aliceSequence, callback);
      },
      ].concat(submits).concat([
      function(callback) {
        tx[3].once('submitted', function(res) {
          // console.log("tx3 submitted: ", res.engine_result);
          assert.strictEqual(res.engine_result, 'tesSUCCESS');

          callback();
        });

        tx[3].submit();
      },
      function(callback) {
        confirmAliceSeq(aliceSequence + 5, callback);
      },
      function(callback) {
        for(let i = 0; i < 4; ++i) {
          tx[i].once('final', function(res) {
            // console.log("tx" + i + " final: ", res.metadata.TransactionResult);
            assert.strictEqual(res.metadata.TransactionResult,
              'tesSUCCESS');
          });
        }
        tx[4].once('final', function(res) {
          // console.log("tx4 final: ", res.metadata.TransactionResult);
          assert.strictEqual(res.metadata.TransactionResult,
            'tesSUCCESS');

          callback();
        });

        $.remote.ledger_accept();
      }
      ]), done);
  });

});
