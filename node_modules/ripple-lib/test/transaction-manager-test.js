/* eslint-disable max-len */

'use strict';

const ws = require('ws');
const lodash = require('lodash');
const assert = require('assert-diff');
const Remote = require('ripple-lib').Remote;
const SerializedObject = require('ripple-lib').SerializedObject;
const Transaction = require('ripple-lib').Transaction;
const TransactionManager = require('ripple-lib')._test.TransactionManager;

const LEDGER = require('./fixtures/transactionmanager').LEDGER;
const ACCOUNT = require('./fixtures/transactionmanager').ACCOUNT;
const ACCOUNT2 = require('./fixtures/transactionmanager').ACCOUNT2;
const SUBSCRIBE_RESPONSE = require('./fixtures/transactionmanager')
.SUBSCRIBE_RESPONSE;
const ACCOUNT_INFO_RESPONSE = require('./fixtures/transactionmanager')
.ACCOUNT_INFO_RESPONSE;
const TX_STREAM_TRANSACTION = require('./fixtures/transactionmanager')
.TX_STREAM_TRANSACTION;
const ACCOUNT_TX_TRANSACTION = require('./fixtures/transactionmanager')
.ACCOUNT_TX_TRANSACTION;
const ACCOUNT_TX_RESPONSE = require('./fixtures/transactionmanager')
.ACCOUNT_TX_RESPONSE;
const ACCOUNT_TX_ERROR = require('./fixtures/transactionmanager')
.ACCOUNT_TX_ERROR;
const SUBMIT_RESPONSE = require('./fixtures/transactionmanager')
.SUBMIT_RESPONSE;
const SUBMIT_TEC_RESPONSE = require('./fixtures/transactionmanager')
.SUBMIT_TEC_RESPONSE;
const SUBMIT_TER_RESPONSE = require('./fixtures/transactionmanager')
.SUBMIT_TER_RESPONSE;
const SUBMIT_TEF_RESPONSE = require('./fixtures/transactionmanager')
.SUBMIT_TEF_RESPONSE;
const SUBMIT_TEL_RESPONSE = require('./fixtures/transactionmanager')
.SUBMIT_TEL_RESPONSE;
const SUBMIT_REMOTE_ERROR = require('./fixtures/transactionmanager')
.SUBMIT_REMOTE_ERROR;
const SUBMIT_TOO_BUSY_ERROR = require('./fixtures/transactionmanager')
.SUBMIT_TOO_BUSY_ERROR;

describe('TransactionManager', function() {
  let rippled;
  let rippledConnection;
  let remote;
  let account;
  let transactionManager;

  beforeEach(function(done) {
    rippled = new ws.Server({port: 5763});

    rippled.on('connection', function(c) {
      const ledger = lodash.extend({}, LEDGER);
      c.sendJSON = function(v) {
        try {
          c.send(JSON.stringify(v));
        } catch (e) /* eslint-disable no-empty */{
          // empty
        } /* eslint-enable no-empty */
      };
      c.sendResponse = function(baseResponse, ext) {
        assert.strictEqual(typeof baseResponse, 'object');
        assert.strictEqual(baseResponse.type, 'response');
        c.sendJSON(lodash.extend(baseResponse, ext));
      };
      c.closeLedger = function() {
        c.sendJSON(lodash.extend(ledger, {
          ledger_index: ++ledger.ledger_index
        }));
      };
      c.on('message', function(m) {
        const parsed = JSON.parse(m);
        rippled.emit('request_' + parsed.command, parsed, c);
      });
      rippledConnection = c;
    });

    rippled.on('request_subscribe', function(message, c) {
      if (lodash.isEqual(message.streams, ['ledger', 'server'])) {
        c.sendResponse(SUBSCRIBE_RESPONSE, {id: message.id});
      }
    });
    rippled.on('request_account_info', function(message, c) {
      if (message.account === ACCOUNT.address) {
        c.sendResponse(ACCOUNT_INFO_RESPONSE, {id: message.id});
      }
    });

    remote = new Remote({servers: ['ws://localhost:5763']});
    remote.setSecret(ACCOUNT.address, ACCOUNT.secret);
    account = remote.account(ACCOUNT.address);
    transactionManager = account._transactionManager;

    remote.connect(function() {
      setTimeout(done, 10);
    });
  });

  afterEach(function(done) {
    remote.disconnect(function() {
      rippled.close();
      setImmediate(done);
    });
  });

  it('Normalize transaction', function() {
    const t1 = TransactionManager.normalizeTransaction(TX_STREAM_TRANSACTION);
    const t2 = TransactionManager.normalizeTransaction(ACCOUNT_TX_TRANSACTION);

    [t1, t2].forEach(function(t) {
      assert(t.hasOwnProperty('metadata'));
      assert(t.hasOwnProperty('tx_json'));
      assert.strictEqual(t.validated, true);
      assert.strictEqual(t.ledger_index, 1);
      assert.strictEqual(t.engine_result, 'tesSUCCESS');
      assert.strictEqual(t.type, 'transaction');
      assert.strictEqual(t.tx_json.hash,
        '01D66ACBD00B2A8F5D66FC8F67AC879CAECF49BC94FB97CF24F66B8406F4C040');
    });
  });

  it('Handle received transaction', function(done) {
    const transaction = Transaction.from_json(TX_STREAM_TRANSACTION.transaction);

    transaction.once('success', function() {
      done();
    });

    transaction.addId(TX_STREAM_TRANSACTION.transaction.hash);
    transactionManager.getPending().push(transaction);
    rippledConnection.sendJSON(TX_STREAM_TRANSACTION);
  });
  it('Handle received transaction -- failed', function(done) {
    const transaction = Transaction.from_json(TX_STREAM_TRANSACTION.transaction);

    transaction.once('error', function(err) {
      assert.strictEqual(err.engine_result, 'tecINSUFF_FEE_P');
      done();
    });

    transaction.addId(TX_STREAM_TRANSACTION.transaction.hash);
    transactionManager.getPending().push(transaction);
    rippledConnection.sendJSON(lodash.extend({ }, TX_STREAM_TRANSACTION, {
      engine_result: 'tecINSUFF_FEE_P'
    }));
  });
  it('Handle received transaction -- not submitted', function(done) {
    rippledConnection.sendJSON(TX_STREAM_TRANSACTION);

    remote.once('transaction', function() {
      assert(transactionManager.getPending().getReceived(
        TX_STREAM_TRANSACTION.transaction.hash));
      done();
    });
  });
  it('Handle received transaction -- Account mismatch', function(done) {
    const tx = lodash.extend({ }, TX_STREAM_TRANSACTION);
    lodash.extend(tx.transaction, {
      Account: 'rMP2Y5EZrVZdFKsow11NoKTE5FjXuBQd3d'
    });
    rippledConnection.sendJSON(tx);

    setImmediate(function() {
      assert(!transactionManager.getPending().getReceived(
        TX_STREAM_TRANSACTION.transaction.hash));
      done();
    });
  });
  it('Handle received transaction -- not validated', function(done) {
    const tx = lodash.extend({ }, TX_STREAM_TRANSACTION, {
      validated: false
    });
    rippledConnection.sendJSON(tx);

    setImmediate(function() {
      assert(!transactionManager.getPending().getReceived(
        TX_STREAM_TRANSACTION.transaction.hash));
      done();
    });
  });
  it('Handle received transaction -- from account_tx', function(done) {
    const transaction = Transaction.from_json(ACCOUNT_TX_TRANSACTION.tx);
    transaction.once('success', function() {
      done();
    });

    transaction.addId(ACCOUNT_TX_TRANSACTION.tx.hash);
    transactionManager.getPending().push(transaction);
    transactionManager._transactionReceived(ACCOUNT_TX_TRANSACTION);
  });

  it('Adjust pending transaction fee', function(done) {
    const transaction = new Transaction(remote);
    transaction.tx_json = ACCOUNT_TX_TRANSACTION.tx;

    transaction.once('fee_adjusted', function(a, b) {
      assert.strictEqual(a, '10');
      assert.strictEqual(b, '24');
      assert.strictEqual(transaction.tx_json.Fee, '24');
      done();
    });

    transactionManager.getPending().push(transaction);

    rippledConnection.sendJSON({
      type: 'serverStatus',
      load_base: 256,
      load_factor: 256 * 2,
      server_status: 'full'
    });
  });

  it('Adjust pending transaction fee -- max fee exceeded', function(done) {
    transactionManager._maxFee = 10;

    const transaction = new Transaction(remote);
    transaction.tx_json = ACCOUNT_TX_TRANSACTION.tx;

    transaction.once('fee_adjusted', function() {
      assert(false, 'Fee should not be adjusted');
    });

    transactionManager.getPending().push(transaction);

    rippledConnection.sendJSON({
      type: 'serverStatus',
      load_base: 256,
      load_factor: 256 * 2,
      server_status: 'full'
    });

    setImmediate(done);
  });

  it('Adjust pending transaction fee -- no local fee', function(done) {
    remote.local_fee = false;

    const transaction = new Transaction(remote);
    transaction.tx_json = ACCOUNT_TX_TRANSACTION.tx;

    transaction.once('fee_adjusted', function() {
      assert(false, 'Fee should not be adjusted');
    });

    transactionManager.getPending().push(transaction);

    rippledConnection.sendJSON({
      type: 'serverStatus',
      load_base: 256,
      load_factor: 256 * 2,
      server_status: 'full'
    });

    setImmediate(done);
  });

  it('Wait ledgers', function(done) {
    transactionManager._waitLedgers(3, done);

    for (let i = 1; i <= 3; i++) {
      rippledConnection.closeLedger();
    }
  });

  it('Wait ledgers -- no ledgers', function(done) {
    transactionManager._waitLedgers(0, done);
  });

  it('Update pending status', function(done) {
    const transaction = Transaction.from_json(TX_STREAM_TRANSACTION.transaction);
    transaction.submitIndex = 1;
    transaction.tx_json.LastLedgerSequence = 10;

    let receivedMissing = false;
    let receivedLost = false;

    transaction.once('missing', function() {
      receivedMissing = true;
    });
    transaction.once('lost', function() {
      receivedLost = true;
    });
    transaction.once('error', function(err) {
      assert.strictEqual(err.engine_result, 'tejMaxLedger');
      assert(receivedMissing);
      assert(receivedLost);
      done();
    });

    transaction.addId(TX_STREAM_TRANSACTION.transaction.hash);
    transactionManager.getPending().push(transaction);

    for (let i = 1; i <= 10; i++) {
      rippledConnection.closeLedger();
    }
  });

  it('Update pending status -- finalized before max ledger exceeded',
    function(done) {
    const transaction = Transaction.from_json(TX_STREAM_TRANSACTION.transaction);
    transaction.submitIndex = 1;
    transaction.tx_json.LastLedgerSequence = 10;
    transaction.finalized = true;

    let receivedMissing = false;
    let receivedLost = false;

    transaction.once('missing', function() {
      receivedMissing = true;
    });
    transaction.once('lost', function() {
      receivedLost = true;
    });
    transaction.once('error', function() {
      assert(false, 'Should not err');
    });

    transaction.addId(TX_STREAM_TRANSACTION.transaction.hash);
    transactionManager.getPending().push(transaction);

    for (let i = 1; i <= 10; i++) {
      rippledConnection.closeLedger();
    }

    setImmediate(function() {
      assert(!receivedMissing);
      assert(!receivedLost);
      done();
    });
  });

  it('Handle reconnect', function(done) {
    const transaction = Transaction.from_json(TX_STREAM_TRANSACTION.transaction);

    const binaryTx = lodash.extend({}, ACCOUNT_TX_TRANSACTION, {
      ledger_index: ACCOUNT_TX_TRANSACTION.tx.ledger_index,
      tx_blob: SerializedObject.from_json(ACCOUNT_TX_TRANSACTION.tx).to_hex(),
      meta: SerializedObject.from_json(ACCOUNT_TX_TRANSACTION.meta).to_hex()
    });

    const hash = new SerializedObject(binaryTx.tx_blob).hash(0x54584E00).to_hex();

    transaction.addId(hash);

    transaction.once('success', function(res) {
      assert.strictEqual(res.engine_result, 'tesSUCCESS');
      done();
    });

    transactionManager.getPending().push(transaction);

    rippled.once('request_account_tx', function(m, req) {
      const response = lodash.extend({}, ACCOUNT_TX_RESPONSE);
      response.result.transactions = [binaryTx];
      req.sendResponse(response, {id: m.id});
    });

    remote.disconnect(remote.connect);
  });

  it('Handle reconnect -- no matching transaction found', function(done) {
    const transaction = Transaction.from_json(TX_STREAM_TRANSACTION.transaction);

    const binaryTx = lodash.extend({}, ACCOUNT_TX_TRANSACTION, {
      ledger_index: ACCOUNT_TX_TRANSACTION.tx.ledger_index,
      tx_blob: SerializedObject.from_json(ACCOUNT_TX_TRANSACTION.tx).to_hex(),
      meta: SerializedObject.from_json(ACCOUNT_TX_TRANSACTION.meta).to_hex()
    });

    transactionManager._request = function() {
      // Resubmitting
      done();
    };

    transactionManager.getPending().push(transaction);

    rippled.once('request_account_tx', function(m, req) {
      const response = lodash.extend({}, ACCOUNT_TX_RESPONSE);
      response.result.transactions = [binaryTx];
      req.sendResponse(response, {id: m.id});
    });

    remote.disconnect(remote.connect);
  });

  it('Handle reconnect -- account_tx error', function(done) {
    const transaction = Transaction.from_json(TX_STREAM_TRANSACTION.transaction);
    transactionManager.getPending().push(transaction);

    transactionManager._resubmit = function() {
      assert(false, 'Should not resubmit');
    };

    rippled.once('request_account_tx', function(m, req) {
      req.sendResponse(ACCOUNT_TX_ERROR, {id: m.id});
      setImmediate(done);
    });

    remote.disconnect(remote.connect);
  });

  it('Submit transaction', function(done) {
    const transaction = remote.createTransaction('AccountSet', {
      account: ACCOUNT.address
    });

    let receivedInitialSuccess = false;
    let receivedProposed = false;
    transaction.once('proposed', function(m) {
      assert.strictEqual(m.engine_result, 'tesSUCCESS');
      receivedProposed = true;
    });
    transaction.once('submitted', function(m) {
      assert.strictEqual(m.engine_result, 'tesSUCCESS');
      receivedInitialSuccess = true;
    });

    rippled.once('request_submit', function(m, req) {
      assert.strictEqual(m.tx_blob, SerializedObject.from_json(
        transaction.tx_json).to_hex());
      assert.strictEqual(new SerializedObject(m.tx_blob).to_json().Sequence,
                         ACCOUNT_INFO_RESPONSE.result.account_data.Sequence);
      assert.strictEqual(transactionManager.getPending().length(), 1);
      req.sendResponse(SUBMIT_RESPONSE, {id: m.id});
      setImmediate(function() {
        const txEvent = lodash.extend({}, TX_STREAM_TRANSACTION);
        txEvent.transaction = transaction.tx_json;
        txEvent.transaction.hash = transaction.hash();
        rippledConnection.sendJSON(txEvent);
      });
    });

    transaction.submit(function(err, res) {
      assert(!err, 'Transaction submission should succeed');
      assert(receivedInitialSuccess);
      assert(receivedProposed);
      assert.strictEqual(res.engine_result, 'tesSUCCESS');
      assert.strictEqual(transactionManager.getPending().length(), 0);
      done();
    });
  });

  it('Submit transaction -- tec error', function(done) {
    const transaction = remote.createTransaction('AccountSet', {
      account: ACCOUNT.address,
      set_flag: 'asfDisableMaster'
    });

    let receivedSubmitted = false;
    transaction.once('proposed', function() {
      assert(false, 'Should not receive proposed event');
    });
    transaction.once('submitted', function(m) {
      assert.strictEqual(m.engine_result, 'tecNO_REGULAR_KEY');
      receivedSubmitted = true;
    });

    rippled.once('request_submit', function(m, req) {
      assert.strictEqual(m.tx_blob, SerializedObject.from_json(
        transaction.tx_json).to_hex());
      assert.strictEqual(new SerializedObject(m.tx_blob).to_json().Sequence,
                         ACCOUNT_INFO_RESPONSE.result.account_data.Sequence);
      assert.strictEqual(transactionManager.getPending().length(), 1);
      req.sendResponse(SUBMIT_TEC_RESPONSE, {id: m.id});
      setImmediate(function() {
        const txEvent = lodash.extend({}, TX_STREAM_TRANSACTION,
          SUBMIT_TEC_RESPONSE.result);
        txEvent.transaction = transaction.tx_json;
        txEvent.transaction.hash = transaction.hash();
        rippledConnection.sendJSON(txEvent);
      });
    });

    transaction.submit(function(err) {
      assert(err, 'Transaction submission should not succeed');
      assert(receivedSubmitted);
      assert.strictEqual(err.engine_result, 'tecNO_REGULAR_KEY');
      assert.strictEqual(transactionManager.getPending().length(), 0);
      done();
    });
  });

  it('Submit transaction -- ter error', function(done) {
    const transaction = remote.createTransaction('Payment', {
      account: ACCOUNT.address,
      destination: ACCOUNT2.address,
      amount: '1'
    });
    transaction.tx_json.Sequence = ACCOUNT_INFO_RESPONSE.result
    .account_data.Sequence + 1;

    let receivedSubmitted = false;
    transaction.once('proposed', function() {
      assert(false, 'Should not receive proposed event');
    });
    transaction.once('submitted', function(m) {
      assert.strictEqual(m.engine_result, SUBMIT_TER_RESPONSE.result.engine_result);
      receivedSubmitted = true;
    });

    rippled.on('request_submit', function(m, req) {
      const deserialized = new SerializedObject(m.tx_blob).to_json();

      switch (deserialized.TransactionType) {
        case 'Payment':
          assert.deepEqual(deserialized, transaction.tx_json);
          assert.strictEqual(transactionManager.getPending().length(), 1);
          req.sendResponse(SUBMIT_TER_RESPONSE, {id: m.id});
          break;
        case 'AccountSet':
          assert.strictEqual(deserialized.Account, ACCOUNT.address);
          assert.strictEqual(deserialized.Flags, 2147483648);
          assert.strictEqual(deserialized.Sequence,
                             ACCOUNT_INFO_RESPONSE.result.account_data.Sequence);
          req.sendResponse(SUBMIT_RESPONSE, {id: m.id});
          req.closeLedger();
          break;
      }
    });
    rippled.once('request_submit', function(m, req) {
      req.sendJSON(lodash.extend({}, LEDGER, {
        ledger_index: transaction.tx_json.LastLedgerSequence + 1
      }));
    });

    transaction.submit(function(err) {
      assert(err, 'Transaction submission should not succeed');
      assert.strictEqual(err.engine_result, 'tejMaxLedger');
      assert(receivedSubmitted);
      assert.strictEqual(transactionManager.getPending().length(), 0);
      assert.strictEqual(transactionManager.getPending().length(), 0);

      const summary = transaction.summary();
      assert.strictEqual(summary.submissionAttempts, 1);
      assert.strictEqual(summary.submitIndex, 2);
      assert.strictEqual(summary.initialSubmitIndex, 2);
      assert.strictEqual(summary.lastLedgerSequence, 5);
      assert.strictEqual(summary.state, 'failed');
      assert.strictEqual(summary.finalized, true);
      assert.deepEqual(summary.result, {
        engine_result: SUBMIT_TER_RESPONSE.result.engine_result,
        engine_result_message: SUBMIT_TER_RESPONSE.result.engine_result_message,
        ledger_hash: undefined,
        ledger_index: undefined,
        transaction_hash: SUBMIT_TER_RESPONSE.result.tx_json.hash
      });
      transactionManager.once('sequence_filled', done);
    });
  });

  it('Submit transaction -- tef error', function(done) {
    const transaction = remote.createTransaction('AccountSet', {
      account: ACCOUNT.address
    });

    transaction.tx_json.Sequence = ACCOUNT_INFO_RESPONSE.result
    .account_data.Sequence - 1;

    let receivedSubmitted = false;
    let receivedResubmitted = false;
    transaction.once('proposed', function() {
      assert(false, 'Should not receive proposed event');
    });
    transaction.once('submitted', function(m) {
      assert.strictEqual(m.engine_result, 'tefPAST_SEQ');
      receivedSubmitted = true;
    });

    rippled.on('request_submit', function(m, req) {
      assert.strictEqual(m.tx_blob, SerializedObject.from_json(
        transaction.tx_json).to_hex());
      assert.strictEqual(transactionManager.getPending().length(), 1);
      req.sendResponse(SUBMIT_TEF_RESPONSE, {id: m.id});
    });

    rippled.once('request_submit', function(m, req) {
      transaction.once('resubmitted', function() {
        receivedResubmitted = true;
        req.sendJSON(lodash.extend({}, LEDGER, {
          ledger_index: transaction.tx_json.LastLedgerSequence + 1
        }));
      });

      req.closeLedger();
    });

    transaction.submit(function(err) {
      assert(err, 'Transaction submission should not succeed');
      assert(receivedSubmitted);
      assert(receivedResubmitted);
      assert.strictEqual(err.engine_result, 'tejMaxLedger');
      assert.strictEqual(transactionManager.getPending().length(), 0);

      const summary = transaction.summary();
      assert.strictEqual(summary.submissionAttempts, 2);
      assert.strictEqual(summary.submitIndex, 3);
      assert.strictEqual(summary.initialSubmitIndex, 2);
      assert.strictEqual(summary.lastLedgerSequence, 5);
      assert.strictEqual(summary.state, 'failed');
      assert.strictEqual(summary.finalized, true);
      assert.deepEqual(summary.result, {
        engine_result: SUBMIT_TEF_RESPONSE.result.engine_result,
        engine_result_message: SUBMIT_TEF_RESPONSE.result.engine_result_message,
        ledger_hash: undefined,
        ledger_index: undefined,
        transaction_hash: SUBMIT_TEF_RESPONSE.result.tx_json.hash
      });
      done();
    });
  });

  it('Submit transaction -- tel error', function(done) {
    const transaction = remote.createTransaction('AccountSet', {
      account: ACCOUNT.address
    });

    let receivedSubmitted = false;
    let receivedResubmitted = false;
    transaction.once('proposed', function() {
      assert(false, 'Should not receive proposed event');
    });
    transaction.once('submitted', function(m) {
      assert.strictEqual(m.engine_result, 'telINSUF_FEE_P');
      receivedSubmitted = true;
    });

    rippled.on('request_submit', function(m, req) {
      assert.strictEqual(m.tx_blob, SerializedObject.from_json(
        transaction.tx_json).to_hex());
      assert.strictEqual(new SerializedObject(m.tx_blob).to_json().Sequence,
                         ACCOUNT_INFO_RESPONSE.result.account_data.Sequence);
      assert.strictEqual(transactionManager.getPending().length(), 1);
      req.sendResponse(SUBMIT_TEL_RESPONSE, {id: m.id});
    });

    rippled.once('request_submit', function(m, req) {
      transaction.once('resubmitted', function() {
        receivedResubmitted = true;
        req.sendJSON(lodash.extend({}, LEDGER, {
          ledger_index: transaction.tx_json.LastLedgerSequence + 1
        }));
      });

      req.closeLedger();
    });

    transaction.submit(function(err) {
      assert(err, 'Transaction submission should not succeed');
      assert(receivedSubmitted);
      assert(receivedResubmitted);
      assert.strictEqual(err.engine_result, 'tejMaxLedger');
      assert.strictEqual(transactionManager.getPending().length(), 0);

      const summary = transaction.summary();
      assert.strictEqual(summary.submissionAttempts, 2);
      assert.strictEqual(summary.submitIndex, 3);
      assert.strictEqual(summary.initialSubmitIndex, 2);
      assert.strictEqual(summary.lastLedgerSequence, 5);
      assert.strictEqual(summary.state, 'failed');
      assert.strictEqual(summary.finalized, true);
      assert.deepEqual(summary.result, {
        engine_result: SUBMIT_TEL_RESPONSE.result.engine_result,
        engine_result_message: SUBMIT_TEL_RESPONSE.result.engine_result_message,
        ledger_hash: undefined,
        ledger_index: undefined,
        transaction_hash: SUBMIT_TEL_RESPONSE.result.tx_json.hash
      });
      done();
    });
  });

  it('Submit transaction -- invalid secret', function(done) {
    remote.setSecret(ACCOUNT.address, ACCOUNT.secret + 'z');

    const transaction = remote.createTransaction('AccountSet', {
      account: ACCOUNT.address
    });

    rippled.once('request_submit', function() {
      assert(false, 'Should not request submit');
    });

    transaction.submit(function(err) {
      assert.strictEqual(err.engine_result, 'tejSecretInvalid');
      assert.strictEqual(transactionManager.getPending().length(), 0);

      const summary = transaction.summary();
      assert.deepEqual(summary.tx_json, transaction.tx_json);
      assert.strictEqual(summary.submissionAttempts, 0);
      assert.strictEqual(summary.submitIndex, undefined);
      assert.strictEqual(summary.initialSubmitIndex, undefined);
      assert.strictEqual(summary.lastLedgerSequence, undefined);
      assert.strictEqual(summary.state, 'failed');
      assert.strictEqual(summary.finalized, true);
      assert.deepEqual(summary.result, {
        engine_result: 'tejSecretInvalid',
        engine_result_message: 'Invalid secret',
        ledger_hash: undefined,
        ledger_index: undefined,
        transaction_hash: undefined
      });
      done();
    });
  });

  it('Submit transaction -- remote error', function(done) {
    const transaction = remote.createTransaction('Payment', {
      account: ACCOUNT.address,
      destination: ACCOUNT2.address,
      amount: '1'
    });

    // MemoType must contain only valid URL characters (RFC 3986). This
    // transaction is invalid
    // transaction.addMemo('my memotype','my_memo_data');
    transaction.tx_json.Memos = [{
      Memo: {
        MemoType: '6D79206D656D6F74797065',
        MemoData: '6D795F6D656D6F5F64617461'
      }
    }];

    let receivedSubmitted = false;
    transaction.once('proposed', function() {
      assert(false, 'Should not receive proposed event');
    });
    transaction.once('submitted', function(m) {
      assert.strictEqual(m.error, 'remoteError');
      receivedSubmitted = true;
    });

    rippled.on('request_submit', function(m, req) {
      assert.strictEqual(m.tx_blob, SerializedObject.from_json(
        transaction.tx_json).to_hex());
      assert.strictEqual(new SerializedObject(m.tx_blob).to_json().Sequence,
                         ACCOUNT_INFO_RESPONSE.result.account_data.Sequence);
      assert.strictEqual(transactionManager.getPending().length(), 1);

      /* eslint-disable max-len */

      // rippled returns an exception here rather than an engine result
      // https://github.com/ripple/rippled/blob/c61d0c663e410c3d3622f20092535710243b55af/src/ripple/rpc/handlers/Submit.cpp#L66-L75

      /* eslint-enable max-len */

      req.sendResponse(SUBMIT_REMOTE_ERROR, {id: m.id});
    });

    transaction.submit(function(err) {
      assert(err, 'Transaction submission should not succeed');
      assert(receivedSubmitted);
      assert.strictEqual(err.error, 'remoteError');
      assert.strictEqual(err.remote.error, 'invalidTransaction');
      assert.strictEqual(transactionManager.getPending().length(), 0);

      const summary = transaction.summary();
      assert.deepEqual(summary.tx_json, transaction.tx_json);
      assert.strictEqual(summary.submissionAttempts, 1);
      assert.strictEqual(summary.submitIndex, 2);
      assert.strictEqual(summary.initialSubmitIndex, 2);
      assert.strictEqual(summary.lastLedgerSequence, 5);
      assert.strictEqual(summary.state, 'failed');
      assert.strictEqual(summary.finalized, true);
      assert.deepEqual(summary.result, {
        engine_result: undefined,
        engine_result_message: undefined,
        ledger_hash: undefined,
        ledger_index: undefined,
        transaction_hash: undefined
      });
      done();
    });
  });

  it('Submit transaction -- disabled resubmission', function(done) {
    const transaction = remote.createTransaction('AccountSet', {
      account: ACCOUNT.address
    });

    transaction.setResubmittable(false);

    let receivedSubmitted = false;
    let receivedResubmitted = false;
    transaction.once('proposed', function() {
      assert(false, 'Should not receive proposed event');
    });
    transaction.once('submitted', function(m) {
      assert.strictEqual(m.engine_result, 'telINSUF_FEE_P');
      receivedSubmitted = true;
    });

    rippled.on('request_submit', function(m, req) {
      assert.strictEqual(m.tx_blob, SerializedObject.from_json(
        transaction.tx_json).to_hex());
      assert.strictEqual(new SerializedObject(m.tx_blob).to_json().Sequence,
                         ACCOUNT_INFO_RESPONSE.result.account_data.Sequence);
      assert.strictEqual(transactionManager.getPending().length(), 1);
      req.sendResponse(SUBMIT_TEL_RESPONSE, {id: m.id});
    });

    rippled.once('request_submit', function(m, req) {
      transaction.once('resubmitted', function() {
        receivedResubmitted = true;
      });

      req.closeLedger();

      setImmediate(function() {
        req.sendJSON(lodash.extend({}, LEDGER, {
          ledger_index: transaction.tx_json.LastLedgerSequence + 1
        }));
      });
    });

    transaction.submit(function(err) {
      assert(err, 'Transaction submission should not succeed');
      assert(receivedSubmitted);
      assert(!receivedResubmitted);
      assert.strictEqual(err.engine_result, 'tejMaxLedger');
      assert.strictEqual(transactionManager.getPending().length(), 0);

      const summary = transaction.summary();
      assert.strictEqual(summary.submissionAttempts, 1);
      assert.strictEqual(summary.submitIndex, 2);
      assert.strictEqual(summary.initialSubmitIndex, 2);
      assert.strictEqual(summary.lastLedgerSequence, 5);
      assert.strictEqual(summary.state, 'failed');
      assert.strictEqual(summary.finalized, true);
      assert.deepEqual(summary.result, {
        engine_result: SUBMIT_TEL_RESPONSE.result.engine_result,
        engine_result_message: SUBMIT_TEL_RESPONSE.result.engine_result_message,
        ledger_hash: undefined,
        ledger_index: undefined,
        transaction_hash: SUBMIT_TEL_RESPONSE.result.tx_json.hash
      });
      done();
    });
  });

  it('Submit transaction -- disabled resubmission -- too busy error', function(done) {
    // Transactions should always be resubmitted in the event of a 'tooBusy'
    // rippled response, even with transaction resubmission disabled

    const transaction = remote.createTransaction('AccountSet', {
      account: ACCOUNT.address
    });

    transaction.setResubmittable(false);

    let receivedSubmitted = false;
    let receivedResubmitted = false;
    transaction.once('proposed', function() {
      assert(false, 'Should not receive proposed event');
    });
    transaction.once('submitted', function() {
      receivedSubmitted = true;
    });

    rippled.on('request_submit', function(m, req) {
      assert.strictEqual(m.tx_blob, SerializedObject.from_json(
        transaction.tx_json).to_hex());
      assert.strictEqual(new SerializedObject(m.tx_blob).to_json().Sequence,
                         ACCOUNT_INFO_RESPONSE.result.account_data.Sequence);
      assert.strictEqual(transactionManager.getPending().length(), 1);

      req.sendResponse(SUBMIT_TOO_BUSY_ERROR, {id: m.id});
    });

    rippled.once('request_submit', function(m, req) {
      transaction.once('resubmitted', function() {
        receivedResubmitted = true;
        req.sendJSON(lodash.extend({}, LEDGER, {
          ledger_index: transaction.tx_json.LastLedgerSequence + 1
        }));
      });

      req.closeLedger();
    });

    transaction.submit(function(err) {
      assert(err, 'Transaction submission should not succeed');
      assert(receivedSubmitted);
      assert(receivedResubmitted);
      assert.strictEqual(err.engine_result, 'tejMaxLedger');
      assert.strictEqual(transactionManager.getPending().length(), 0);

      const summary = transaction.summary();
      assert.strictEqual(summary.submissionAttempts, 2);
      assert.strictEqual(summary.submitIndex, 3);
      assert.strictEqual(summary.initialSubmitIndex, 2);
      assert.strictEqual(summary.lastLedgerSequence, 5);
      assert.strictEqual(summary.state, 'failed');
      assert.strictEqual(summary.finalized, true);
      assert.deepEqual(summary.result, {
        engine_result: undefined,
        engine_result_message: undefined,
        ledger_hash: undefined,
        ledger_index: undefined,
        transaction_hash: undefined
      });
      done();
    });
  });
});
