/* -------------------------------- REQUIRES -------------------------------- */

var async       = require("async");
var assert      = require('assert');
var UInt160     = require("ripple-lib").UInt160;
var testutils   = require("./testutils");
var config      = testutils.init_config();

/* --------------------------------- CONSTS --------------------------------- */

// some parts of the test take a LONG time
var SKIP_TICKET_EXPIRY_PHASE = !process.env.CI &&
                               !process.env.RUN_TICKET_EXPIRY;

var ROOT_ACCOUNT = UInt160.json_rewrite('root');
var ALICE_ACCOUNT = UInt160.json_rewrite('alice');

/* --------------------------------- HELPERS -------------------------------- */

var prettyj = function(obj) {
  return JSON.stringify(obj, undefined, 2);
}

// The create a transaction, submit it, pass a ledger pattern is pervasive
var submit_transaction_factory = function(remote) {
  return function(kwargs, cb) {
    tx = remote.transaction();
    tx.tx_json = kwargs;
    tx.submit(cb);
    testutils.ledger_wait(remote, tx);
  };
};

/* ---------------------------------- TESTS --------------------------------- */

suite.skip("Ticket tests", function() {
  var $ = { };
  var submit_tx;

  setup(function(done) {
    testutils.build_setup().call($, function(){
      submit_tx = submit_transaction_factory($.remote);
      done();
    });
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test("Delete a non existent ticket", function(done) {
    submit_tx(
      {
        TransactionType: 'TicketCancel',
        Account:  ROOT_ACCOUNT,
        TicketID: '0000000000000000000000000000000000000000000000000000000000000001'
      },
      function(err, message){
        assert.equal(err.engine_result, 'tecNO_ENTRY');
        done();
      }
    );
  });

  test("Create a ticket with non existent Target", function(done) {
    submit_tx(
      {
        TransactionType: 'TicketCreate',
        Account: ROOT_ACCOUNT,
        Target: ALICE_ACCOUNT
      },
      function(err, message){
        assert.equal(err.engine_result, 'tecNO_TARGET');
        done();
      }
    );
  });

  test("Create a ticket where Target == Account. Target unsaved", function(done) {
    submit_tx(
      {
        Account: ROOT_ACCOUNT,
        Target : ROOT_ACCOUNT,
        TransactionType:'TicketCreate'
      },
      function(err, message){
        assert.ifError(err);
        assert.deepEqual(
          message.metadata.AffectedNodes[1],
          {"CreatedNode":
            {"LedgerEntryType": "Ticket",
             "LedgerIndex": "7F58A0AE17775BA3404D55D406DD1C2E91EADD7AF3F03A26877BCE764CCB75E3",
             /*Note there's no `Target` saved in the ticket */
             "NewFields": {"Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
                           "Sequence": 1}}});

        done();
      }
    );
  });

  test("Create a ticket, then delete by creator", function (done) {
    var steps = [
      function(callback) {
        submit_tx(
          {
            TransactionType: 'TicketCreate',
            Account: ROOT_ACCOUNT,
          },
          function(err, message){
            var affected = message.metadata.AffectedNodes;
            var account = affected[0].ModifiedNode;
            var ticket = affected[1].CreatedNode;

            assert.equal(ticket.LedgerEntryType, 'Ticket');
            assert.equal(account.LedgerEntryType, 'AccountRoot');
            assert.equal(account.PreviousFields.OwnerCount, 0);
            assert.equal(account.FinalFields.OwnerCount, 1);
            assert.equal(ticket.NewFields.Sequence, account.PreviousFields.Sequence);
            assert.equal(ticket.LedgerIndex, '7F58A0AE17775BA3404D55D406DD1C2E91EADD7AF3F03A26877BCE764CCB75E3');

            callback();
          }
        );
      },
      function delete_ticket(callback) {
        submit_tx(
          {
            TransactionType: 'TicketCancel',
            Account: ROOT_ACCOUNT,
            TicketID: '7F58A0AE17775BA3404D55D406DD1C2E91EADD7AF3F03A26877BCE764CCB75E3'
          },
          function(err, message){
            assert.ifError(err);
            assert.equal(message.engine_result, 'tesSUCCESS');

            var affected = message.metadata.AffectedNodes;
            var account = affected[0].ModifiedNode;
            var ticket = affected[1].DeletedNode;
            var directory = affected[2].DeletedNode;

            assert.equal(ticket.LedgerEntryType, 'Ticket');
            assert.equal(account.LedgerEntryType, 'AccountRoot');
            assert.equal(directory.LedgerEntryType, 'DirectoryNode');

            callback();
          }
        );
      }
    ]
    async.waterfall(steps, done.bind(this));
  });

  test("Expiration - future", function (done) {
    // 1000 seconds is an arbitrarily chosen amount of time to expire the
    // ticket. The main thing is that it's not in the past (no ticket ledger
    // entry would be created) or 0 (malformed)
    seconds_from_now = 1000;

    var expiration = $.remote._ledger_time + seconds_from_now;

    submit_tx(
      {
        Account: ROOT_ACCOUNT,
        TransactionType: 'TicketCreate',
        Expiration: expiration,
      },
      function(err, message){
        assert.ifError(err);

        var affected = message.metadata.AffectedNodes;
        var account = affected[0].ModifiedNode;
        var ticket = affected[1].CreatedNode;

        assert.equal(ticket.LedgerEntryType, 'Ticket');
        assert.equal(account.LedgerEntryType, 'AccountRoot');
        assert.equal(account.PreviousFields.OwnerCount, 0);
        assert.equal(account.FinalFields.OwnerCount, 1);
        assert.equal(ticket.NewFields.Sequence, account.PreviousFields.Sequence);
        assert.equal(ticket.NewFields.Expiration, expiration);

        done();
      }
    );
  });

  test("Expiration - past", function (done) {
    var expiration = $.remote._ledger_time - 1000;

    submit_tx(
      {
        Account: ROOT_ACCOUNT,
        TransactionType: 'TicketCreate',
        Expiration: expiration,
      },
      function(err, message){
        assert.ifError(err);
        assert.equal(message.engine_result, 'tesSUCCESS');
        var affected = message.metadata.AffectedNodes;
        assert.equal(affected.length, 1); // just the account
        var account = affected[0].ModifiedNode;
        assert.equal(account.FinalFields.Account, ROOT_ACCOUNT);

        done();
      }
    );
  });

  test("Expiration - zero", function (done) {
    var expiration = 0;

    submit_tx(
      {
        Account: ROOT_ACCOUNT,
        TransactionType: 'TicketCreate',
        Expiration: expiration,
      },
      function(err, message) {
        assert.equal(err.engine_result, 'temBAD_EXPIRATION');
        done();
      }
    );
  });

  test("Create a ticket, delete by Target", function (done) {
    var steps = [
      function create_alice(callback) {
        testutils.create_accounts($.remote, "root", "10000.0", ["alice"], callback);
      },
      function create_ticket(callback) {
        var Account = ROOT_ACCOUNT;
        var Target = ALICE_ACCOUNT;
        submit_tx(
          {
            TransactionType: 'TicketCreate',
            Account: Account,
            Target: Target,
          },
          function(err, message) {
            assert.ifError(err);
            assert.deepEqual(message.metadata.AffectedNodes[1],
              {
              CreatedNode: {
                LedgerEntryType: "Ticket",
                LedgerIndex: "C231BA31A0E13A4D524A75F990CE0D6890B800FF1AE75E51A2D33559547AC1A2",
                NewFields: {
                  Account: Account,
                  Target: Target,
                  Sequence: 2,
                }
              }
            });
            callback();
          }
        );
      },
      function alice_cancels_ticket(callback) {
        submit_tx(
          {
            Account: ALICE_ACCOUNT,
            TransactionType: 'TicketCancel',
            TicketID: 'C231BA31A0E13A4D524A75F990CE0D6890B800FF1AE75E51A2D33559547AC1A2',
          },
          function(err, message) {
            assert.ifError(err);
            assert.equal(message.engine_result, 'tesSUCCESS');
            assert.deepEqual(
              message.metadata.AffectedNodes[2],
              {"DeletedNode":
                {"FinalFields": {
                  "Account": ROOT_ACCOUNT,
                  "Flags": 0,
                  "OwnerNode": "0000000000000000",
                  "Sequence": 2,
                  "Target": ALICE_ACCOUNT},
                "LedgerEntryType": "Ticket",
                "LedgerIndex":
                  "C231BA31A0E13A4D524A75F990CE0D6890B800FF1AE75E51A2D33559547AC1A2"}}
            );
            callback();
          }
        );
      }
    ]
    async.waterfall(steps, done.bind(this));
  });

  test("Create a ticket, let it expire, delete by a random", function (done) {
    this.timeout(55000);
    var remote = $.remote;
    var expiration = $.remote._ledger_time + 1;

    steps = [
      function create_ticket(callback) {
        submit_tx(
          {
            Account: ROOT_ACCOUNT,
            TransactionType: 'TicketCreate',
            Expiration: expiration,

          },
          function(err, message) {
            callback(null, message);
          }
        );
      },
      function verify_creation(message, callback){
        var affected = message.metadata.AffectedNodes;
        var account = affected[0].ModifiedNode;
        var ticket = affected[1].CreatedNode;

        assert.equal(ticket.LedgerEntryType, 'Ticket');
        assert.equal(account.LedgerEntryType, 'AccountRoot');
        assert.equal(account.PreviousFields.OwnerCount, 0);
        assert.equal(account.FinalFields.OwnerCount, 1);
        assert.equal(ticket.NewFields.Sequence, account.PreviousFields.Sequence);
        assert.equal(ticket.LedgerIndex, '7F58A0AE17775BA3404D55D406DD1C2E91EADD7AF3F03A26877BCE764CCB75E3');

        callback();
      },
      function create_account_for_issuing_expiration(callback){
        testutils.create_accounts($.remote,
                                  "root", "1000.0", ["alice"], callback);
      },
      function delete_ticket(callback) {
        submit_tx(
          {
            TransactionType: 'TicketCancel',
            Account: ALICE_ACCOUNT,
            TicketID: '7F58A0AE17775BA3404D55D406DD1C2E91EADD7AF3F03A26877BCE764CCB75E3',

          },
          function(err, message) {
            // at this point we are unauthorized
            // but it should be expired soon!
            assert.equal(err.engine_result, 'tecNO_PERMISSION');
            callback();
          }
        );
      },
      function close_ledger(callback) {
        if (SKIP_TICKET_EXPIRY_PHASE) {
          return done();
        };

        setTimeout(callback, 10001);
      },
      function close_ledger(callback) {
        remote.ledger_accept(function(){callback();});
      },
      function close_ledger(callback) {
        setTimeout(callback, 10001);
      },
      function close_ledger(callback) {
        remote.ledger_accept(function(){callback();});
      },
      function close_ledger(callback) {
        setTimeout(callback, 10001);
      },
      function close_ledger(callback) {
        remote.ledger_accept(function(){callback();});
      },
      function delete_ticket(callback) {
        submit_tx(
          {
            TransactionType: 'TicketCancel',
            Account: ALICE_ACCOUNT,
            TicketID: '7F58A0AE17775BA3404D55D406DD1C2E91EADD7AF3F03A26877BCE764CCB75E3',
          },
          function(err, message) {
            callback(null, message);
          }
        );
      },

      function verify_deletion (message, callback){
        assert.equal(message.engine_result, 'tesSUCCESS');

        var affected = message.metadata.AffectedNodes;
        var account = affected[0].ModifiedNode;
        var ticket = affected[1].DeletedNode;
        var account2 = affected[2].ModifiedNode;
        var directory = affected[3].DeletedNode;

        assert.equal(ticket.LedgerEntryType, 'Ticket');
        assert.equal(account.LedgerEntryType, 'AccountRoot');
        assert.equal(directory.LedgerEntryType, 'DirectoryNode');
        assert.equal(account2.LedgerEntryType, 'AccountRoot');

        callback();
      }
    ]
    async.waterfall(steps, done.bind(this));
  });
});
// vim:sw=2:sts=2:ts=8:etq
