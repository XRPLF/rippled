/* eslint-disable max-len */

'use strict';

const assert = require('assert');
const lodash = require('lodash');
const Transaction = require('ripple-lib').Transaction;
const TransactionQueue = require('ripple-lib').TransactionQueue;
const Remote = require('ripple-lib').Remote;
const Server = require('ripple-lib').Server;

const transactionResult = {
  engine_result: 'tesSUCCESS',
  engine_result_code: 0,
  engine_result_message: 'The transaction was applied.',
  ledger_hash: '2031E311FD28A6B37697BD6ECF8E6661521902E7A6A8EF069A2F3C628E76A322',
  ledger_index: 7106144,
  status: 'closed',
  type: 'transaction',
  validated: true,
  metadata: {
    AffectedNodes: [ ],
    TransactionIndex: 0,
    TransactionResult: 'tesSUCCESS'
  },
  tx_json: {
    Account: 'rHPotLj3CNKaP4bQANcecEuT8hai3VpxfB',
    Amount: '1000000',
    Destination: 'rYtn3D1VGQyf1MTqcwLDepUKm22YEGXGJA',
    Fee: '10',
    Flags: 0,
    LastLedgerSequence: 7106151,
    Sequence: 2973,
    SigningPubKey: '0306E9F38DF11402953A5B030C1AE8A88C47E348170C3B8EC6C8D775E797168462',
    TransactionType: 'Payment',
    TxnSignature: '3045022100A58B0460BC5092CB4F96155C19125A4E079C870663F1D5E8BBC9BDEE06D51F530220408A3AA26988ABF18E16BE77B016F25018A2AA7C99FFE723FC8598471357DBCF',
    date: 455660500,
    hash: '61D60378AB70ACE630B20A81B50708A3DB5E7CEE35914292FF3761913DA61DEA'
  }
};

// https://github.com/ripple/rippled/blob/c61d0c663e410c3d3622f20092535710243b55af/src/ripple/protocol/impl/STTx.cpp#L342-L370
const allowed_memo_chars = ('0123456789-._~:/?#[]@!$&\'()*+,;=%ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz').split('');

// Disallowed ASCII characters
const disallowed_memo_chars = [];

for (let i = 0; i <= 127; i++) {
  const char = String.fromCharCode(i);
  if (!lodash.contains(allowed_memo_chars, char)) {
    disallowed_memo_chars.push(char);
  }
}

describe('Transaction', function() {
  it('Success listener', function(done) {
    const transaction = new Transaction();

    transaction.once('final', function(message) {
      assert.deepEqual(message, transactionResult);
      assert(transaction.finalized);
      assert.strictEqual(transaction.state, 'validated');
      done();
    });

    transaction.emit('success', transactionResult);
  });

  it('Error listener', function(done) {
    const transaction = new Transaction();

    transaction.once('final', function(message) {
      assert.deepEqual(message, transactionResult);
      assert(transaction.finalized);
      assert.strictEqual(transaction.state, 'failed');
      done();
    });

    transaction.emit('error', transactionResult);
  });

  it('Submitted listener', function() {
    const transaction = new Transaction();
    transaction.emit('submitted');
    assert.strictEqual(transaction.state, 'submitted');
  });

  it('Proposed listener', function() {
    const transaction = new Transaction();
    transaction.emit('proposed');
    assert.strictEqual(transaction.state, 'pending');
  });

  it('Check response code is tel', function() {
    const transaction = new Transaction();
    assert(!transaction.isTelLocal(-400));
    assert(transaction.isTelLocal(-399));
    assert(transaction.isTelLocal(-300));
    assert(!transaction.isTelLocal(-299));
  });

  it('Check response code is tem', function() {
    const transaction = new Transaction();
    assert(!transaction.isTemMalformed(-300));
    assert(transaction.isTemMalformed(-299));
    assert(transaction.isTemMalformed(-200));
    assert(!transaction.isTemMalformed(-199));
  });

  it('Check response code is tef', function() {
    const transaction = new Transaction();
    assert(!transaction.isTefFailure(-200));
    assert(transaction.isTefFailure(-199));
    assert(transaction.isTefFailure(-100));
    assert(!transaction.isTefFailure(-99));
  });

  it('Check response code is ter', function() {
    const transaction = new Transaction();
    assert(!transaction.isTerRetry(-100));
    assert(transaction.isTerRetry(-99));
    assert(transaction.isTerRetry(-1));
    assert(!transaction.isTerRetry(0));
  });

  it('Check response code is tep', function() {
    const transaction = new Transaction();
    assert(!transaction.isTepSuccess(-1));
    assert(transaction.isTepSuccess(0));
    assert(transaction.isTepSuccess(1e3));
  });

  it('Check response code is tec', function() {
    const transaction = new Transaction();
    assert(!transaction.isTecClaimed(99));
    assert(transaction.isTecClaimed(100));
    assert(transaction.isTecClaimed(1e3));
  });

  it('Check response code is rejected', function() {
    const transaction = new Transaction();
    assert(!transaction.isRejected(0));
    assert(!transaction.isRejected(-99));
    assert(transaction.isRejected(-100));
    assert(transaction.isRejected(-399));
    assert(!transaction.isRejected(-400));
  });

  it('Set state', function(done) {
    const transaction = new Transaction();

    assert.strictEqual(transaction.state, 'unsubmitted');

    let receivedEvents = 0;
    const events = [
      'submitted',
      'pending',
      'validated'
    ];

    transaction.on('state', function(state) {
      receivedEvents++;
      assert(events.indexOf(state) > -1, 'Invalid state: ' + state);
    });

    transaction.setState(events[0]);
    transaction.setState(events[1]);
    transaction.setState(events[1]);
    transaction.setState(events[2]);

    assert.strictEqual(receivedEvents, 3);
    assert.strictEqual(transaction.state, events[2]);
    done();
  });

  it('Finalize submission', function() {
    const transaction = new Transaction();

    const tx = transactionResult;
    tx.ledger_hash = '2031E311FD28A6BZ76Z7BD6ECF8E6661521902E7A6A8EF069A2F3C628E76A322';
    tx.ledger_index = 7106150;

    transaction.result = transactionResult;
    transaction.finalize(tx);

    assert.strictEqual(transaction.result.ledger_index, tx.ledger_index);
    assert.strictEqual(transaction.result.ledger_hash, tx.ledger_hash);
  });

  it('Finalize unsubmitted', function() {
    const transaction = new Transaction();
    transaction.finalize(transactionResult);

    assert.strictEqual(transaction.result.ledger_index, transactionResult.ledger_index);
    assert.strictEqual(transaction.result.ledger_hash, transactionResult.ledger_hash);
  });

  it('Get account secret', function() {
    const remote = new Remote();

    remote.secrets = {
      rpzT237Ctpaa58KieifoK8RyBmmRwEcfhK: 'shY1njzHAXp8Qt3bpxYW6RpoZtMKP',
      rpdxPs9CR93eLAc5DTvAgv4S9XJ1CzKj1a: 'ssboTJezioTq8obyvDU9tVo95NGGQ'
    };

    const transaction = new Transaction(remote);

    assert.strictEqual(transaction._accountSecret('rpzT237Ctpaa58KieifoK8RyBmmRwEcfhK'), 'shY1njzHAXp8Qt3bpxYW6RpoZtMKP');
    assert.strictEqual(transaction._accountSecret('rpdxPs9CR93eLAc5DTvAgv4S9XJ1CzKj1a'), 'ssboTJezioTq8obyvDU9tVo95NGGQ');
    assert.strictEqual(transaction._accountSecret('rExistNot'), undefined);
  });

  it('Get fee units', function() {
    const remote = new Remote();
    const transaction = new Transaction(remote);
    assert.strictEqual(transaction.feeUnits(), 10);
  });

  it('Compute fee', function() {
    const remote = new Remote();

    const s1 = new Server(remote, 'wss://s-west.ripple.com:443');
    s1._connected = true;

    const s2 = new Server(remote, 'wss://s-west.ripple.com:443');
    s2._connected = true;
    s2._load_factor = 256 * 4;

    const s3 = new Server(remote, 'wss://s-west.ripple.com:443');
    s3._connected = true;
    s3._load_factor = 256 * 8;

    const s4 = new Server(remote, 'wss://s-west.ripple.com:443');
    s4._connected = true;
    s4._load_factor = 256 * 8;

    const s5 = new Server(remote, 'wss://s-west.ripple.com:443');
    s5._connected = true;
    s5._load_factor = 256 * 7;

    remote._servers = [s2, s3, s1, s4];

    assert.strictEqual(s1._computeFee(10), '12');
    assert.strictEqual(s2._computeFee(10), '48');
    assert.strictEqual(s3._computeFee(10), '96');
    assert.strictEqual(s4._computeFee(10), '96');
    assert.strictEqual(s5._computeFee(10), '84');

    const transaction = new Transaction(remote);

    assert.strictEqual(transaction._computeFee(), '72');
  });

  it('Compute fee, no remote', function() {
    const transaction = new Transaction();
    assert.strictEqual(transaction._computeFee(10), undefined);
  });

  it('Compute fee - no connected server', function() {
    const remote = new Remote();

    const s1 = new Server(remote, 'wss://s-west.ripple.com:443');
    s1._connected = false;

    const s2 = new Server(remote, 'wss://s-west.ripple.com:443');
    s2._connected = false;
    s2._load_factor = 256 * 4;

    const s3 = new Server(remote, 'wss://s-west.ripple.com:443');
    s3._connected = false;
    s3._load_factor = 256 * 8;

    remote._servers = [s1, s2, s3];

    assert.strictEqual(s1._computeFee(10), '12');
    assert.strictEqual(s2._computeFee(10), '48');
    assert.strictEqual(s3._computeFee(10), '96');

    const transaction = new Transaction(remote);

    assert.strictEqual(transaction._computeFee(), undefined);
  });

  it('Compute fee - one connected server', function() {
    const remote = new Remote();

    const s1 = new Server(remote, 'wss://s-west.ripple.com:443');
    s1._connected = false;

    const s2 = new Server(remote, 'wss://s-west.ripple.com:443');
    s2._connected = false;
    s2._load_factor = 256 * 4;

    const s3 = new Server(remote, 'wss://s-west.ripple.com:443');
    s3._connected = true;
    s3._load_factor = 256 * 8;

    remote._servers = [s1, s2, s3];

    assert.strictEqual(s1._computeFee(10), '12');
    assert.strictEqual(s2._computeFee(10), '48');
    assert.strictEqual(s3._computeFee(10), '96');

    const transaction = new Transaction(remote);

    assert.strictEqual(transaction._computeFee(), '96');
  });

  it('Does not compute a median fee with floating point', function() {
    const remote = new Remote();

    const s1 = new Server(remote, 'wss://s-west.ripple.com:443');
    s1._connected = true;

    const s2 = new Server(remote, 'wss://s-west.ripple.com:443');
    s2._connected = true;
    s2._load_factor = 256 * 4;

    const s3 = new Server(remote, 'wss://s-west.ripple.com:443');
    s3._connected = true;

    s3._load_factor = (256 * 7) + 1;

    const s4 = new Server(remote, 'wss://s-west.ripple.com:443');
    s4._connected = true;
    s4._load_factor = 256 * 16;

    remote._servers = [s1, s2, s3, s4];

    assert.strictEqual(s1._computeFee(10), '12');
    assert.strictEqual(s2._computeFee(10), '48');
    // 66.5
    assert.strictEqual(s3._computeFee(10), '85');
    assert.strictEqual(s4._computeFee(10), '192');

    const transaction = new Transaction(remote);
    transaction.tx_json.Sequence = 1;
    const src = 'rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh';
    const dst = 'rGihwhaqU8g7ahwAvTq6iX5rvsfcbgZw6v';

    transaction.payment(src, dst, '100');
    remote.setSecret(src, 'masterpassphrase');

    assert(transaction.complete());
    const json = transaction.serialize().to_json();
    assert.notStrictEqual(json.Fee, '66500000', 'Fee == 66500000, i.e. 66.5 XRP!');
  });


  it('Compute fee - even server count', function() {
    const remote = new Remote();

    const s1 = new Server(remote, 'wss://s-west.ripple.com:443');
    s1._connected = true;

    const s2 = new Server(remote, 'wss://s-west.ripple.com:443');
    s2._connected = true;
    s2._load_factor = 256 * 4;

    const s3 = new Server(remote, 'wss://s-west.ripple.com:443');
    s3._connected = true;
    s3._load_factor = 256 * 8;

    const s4 = new Server(remote, 'wss://s-west.ripple.com:443');
    s4._connected = true;
    s4._load_factor = 256 * 16;

    remote._servers = [s1, s2, s3, s4];

    assert.strictEqual(s1._computeFee(10), '12');
    assert.strictEqual(s2._computeFee(10), '48');
    // 72
    assert.strictEqual(s3._computeFee(10), '96');
    assert.strictEqual(s4._computeFee(10), '192');

    const transaction = new Transaction(remote);

    assert.strictEqual(transaction._computeFee(), '72');
  });

  it('Complete transaction', function(done) {
    const remote = new Remote();

    const s1 = new Server(remote, 'wss://s-west.ripple.com:443');
    s1._connected = true;

    remote._servers = [s1];
    remote.trusted = true;
    remote.local_signing = true;

    const transaction = new Transaction(remote);
    transaction._secret = 'sh2pTicynUEG46jjR4EoexHcQEoij';
    transaction.tx_json.Account = 'rMWwx3Ma16HnqSd4H6saPisihX9aKpXxHJ';
    transaction.tx_json.Flags = 0;

    assert(transaction.complete());

    done();
  });

  it('Complete transaction, local signing, no remote', function(done) {
    const transaction = new Transaction();
    transaction._secret = 'sh2pTicynUEG46jjR4EoexHcQEoij';
    transaction.tx_json.Account = 'rMWwx3Ma16HnqSd4H6saPisihX9aKpXxHJ';

    assert(transaction.complete());

    done();
  });

  it('Complete transaction - untrusted', function(done) {
    const remote = new Remote();
    const transaction = new Transaction(remote);

    remote.trusted = false;
    remote.local_signing = false;

    transaction.once('error', function(err) {
      assert.strictEqual(err.result, 'tejServerUntrusted');
      done();
    });

    assert(!transaction.complete());
  });

  it('Complete transaction - no secret', function(done) {
    const remote = new Remote();
    const transaction = new Transaction(remote);

    remote.trusted = true;
    remote.local_signing = true;

    transaction.once('error', function(err) {
      assert.strictEqual(err.result, 'tejSecretUnknown');
      done();
    });

    assert(!transaction.complete());
  });

  it('Complete transaction - invalid secret', function(done) {
    const remote = new Remote();
    const transaction = new Transaction(remote);

    remote.trusted = true;
    remote.local_signing = true;

    transaction.SigningPubKey = undefined;
    transaction.tx_json.Account = 'rMWwx3Ma16HnqSd4H6saPisihX9aKpXxHJ';
    transaction._secret = 'sh2pTicynUEG46jjR4EoexHcQEoijX';
    transaction.once('error', function(err) {
      assert.strictEqual(err.result, 'tejSecretInvalid');
      done();
    });

    assert(!transaction.complete());
  });

  it('Complete transaction - cached SigningPubKey', function(done) {
    const remote = new Remote();
    const transaction = new Transaction(remote);

    remote.trusted = true;
    remote.local_signing = true;

    transaction.tx_json.SigningPubKey = '021FED5FD081CE5C4356431267D04C6E2167E4112C897D5E10335D4E22B4DA49ED';
    transaction.tx_json.Account = 'rMWwx3Ma16HnqSd4H6saPisihX9aKpXxHJ';
    transaction._secret = 'sh2pTicynUEG46jjR4EoexHcQEoijX';

    transaction.once('error', function(err) {
      assert.notStrictEqual(err.result, 'tejSecretInvalid');
      done();
    });

    assert(!transaction.complete());
  });

  it('Complete transaction - compute fee', function(done) {
    const remote = new Remote();
    const transaction = new Transaction(remote);

    const s1 = new Server(remote, 'wss://s-west.ripple.com:443');
    s1._connected = true;

    remote._servers = [s1];
    remote.trusted = true;
    remote.local_signing = true;

    transaction.tx_json.SigningPubKey = '021FED5FD081CE5C4356431267D04C6E2167E4112C897D5E10335D4E22B4DA49ED';
    transaction.tx_json.Account = 'rMWwx3Ma16HnqSd4H6saPisihX9aKpXxHJ';
    transaction._secret = 'sh2pTicynUEG46jjR4EoexHcQEoij';

    assert.strictEqual(transaction.tx_json.Fee, undefined);

    assert(transaction.complete());

    assert.strictEqual(transaction.tx_json.Fee, '12');

    done();
  });

  it('Complete transaction - compute fee exceeds max fee', function(done) {
    const remote = new Remote({max_fee: 10});

    const s1 = new Server(remote, 'wss://s-west.ripple.com:443');
    s1._connected = true;
    s1._load_factor = 256 * 16;

    remote._servers = [s1];
    remote.trusted = true;
    remote.local_signing = true;

    const transaction = new Transaction(remote);
    transaction.tx_json.SigningPubKey = '021FED5FD081CE5C4356431267D04C6E2167E4112C897D5E10335D4E22B4DA49ED';
    transaction.tx_json.Account = 'rMWwx3Ma16HnqSd4H6saPisihX9aKpXxHJ';
    transaction._secret = 'sh2pTicynUEG46jjR4EoexHcQEoij';

    transaction.once('error', function(err) {
      assert.strictEqual(err.result, 'tejMaxFeeExceeded');
      done();
    });

    assert(!transaction.complete());
  });

  it('Complete transaction - canonical flags', function(done) {
    const remote = new Remote();

    const s1 = new Server(remote, 'wss://s-west.ripple.com:443');
    s1._connected = true;
    s1._load_factor = 256;

    remote._servers = [s1];
    remote.trusted = true;
    remote.local_signing = true;

    const transaction = new Transaction(remote);
    transaction._secret = 'sh2pTicynUEG46jjR4EoexHcQEoij';
    transaction.tx_json.SigningPubKey = '021FED5FD081CE5C4356431267D04C6E2167E4112C897D5E10335D4E22B4DA49ED';
    transaction.tx_json.Account = 'rMWwx3Ma16HnqSd4H6saPisihX9aKpXxHJ';
    transaction.tx_json.Flags = 0;

    assert(transaction.complete());
    assert.strictEqual(transaction.tx_json.Flags, 2147483648);

    done();
  });

  describe('ed25519 signing', function() {
    it('can accept an ed25519 seed for ._secret', function() {
      const expectedPub = 'EDD3993CDC6647896C455F136648B7750' +
                          '723B011475547AF60691AA3D7438E021D';

      const expectedSig = 'C3646313B08EED6AF4392261A31B961F' +
                          '10C66CB733DB7F6CD9EAB079857834C8' +
                          'B0334270A2C037E63CDCCC1932E08328' +
                          '82B7B7066ECD2FAEDEB4A83DF8AE6303';

      const tx_json = {
        Account: 'rJZdUusLDtY9NEsGea7ijqhVrXv98rYBYN',
        Amount: '1000',
        Destination: 'rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh',
        Fee: '10',
        Flags: 2147483648,
        Sequence: 1,
        TransactionType: 'Payment'
      };

      const tx = Transaction.from_json(tx_json);
      tx.setSecret('sEd7rBGm5kxzauRTAV2hbsNz7N45X91');
      tx.complete();
      tx.sign();

      assert.strictEqual(tx_json.SigningPubKey, expectedPub);
      assert.strictEqual(tx_json.TxnSignature, expectedSig);
    });
  });

  describe('signing', function() {
    const tx_json = {
      SigningPubKey: '021FED5FD081CE5C4356431267D04C6E2167E4112C897D5E10335D4E22B4DA49ED',
      Account: 'rMWwx3Ma16HnqSd4H6saPisihX9aKpXxHJ',
      Flags: 0,
      Fee: 10,
      Sequence: 1,
      TransactionType: 'AccountSet'
    };

    const expectedSigningHash =
      'D1C15200CF532175F1890B6440AD223D3676140522BC11D2784E56760AE3B4FE';

    it('Get signing hash', function() {
      const transaction = Transaction.from_json(tx_json);
      transaction._secret = 'sh2pTicynUEG46jjR4EoexHcQEoij';
      assert.strictEqual(transaction.signingHash(), expectedSigningHash);
    });

    it('Get signing data', function() {
      const tx = Transaction.from_json(tx_json);
      const data = tx.signingData();

      assert.strictEqual(data.hash().to_json(),
                         expectedSigningHash);

      assert.strictEqual(data.to_hex(),
        ('535458001200032200000000240000000168400000000000000' +
         'A7321021FED5FD081CE5C4356431267D04C6E2167E4112C897D' +
         '5E10335D4E22B4DA49ED8114E0E6E281CA324AEE034B2BB8AC9' +
         '7BA1ACA95A068'));
    });
  });

  it('Get hash - no prefix', function(done) {
    const transaction = new Transaction();
    transaction._secret = 'sh2pTicynUEG46jjR4EoexHcQEoij';
    transaction.tx_json.SigningPubKey = '021FED5FD081CE5C4356431267D04C6E2167E4112C897D5E10335D4E22B4DA49ED';
    transaction.tx_json.Account = 'rMWwx3Ma16HnqSd4H6saPisihX9aKpXxHJ';
    transaction.tx_json.Flags = 0;
    transaction.tx_json.Fee = 10;
    transaction.tx_json.Sequence = 1;
    transaction.tx_json.TransactionType = 'AccountSet';

    assert.strictEqual(transaction.hash(), '1A860FC46D1DD9200560C64002418A4E8BBDE939957AC82D7B14D80A1C0E2EB5');

    done();
  });

  it('Get hash - prefix', function(done) {
    const transaction = new Transaction();
    transaction._secret = 'sh2pTicynUEG46jjR4EoexHcQEoij';
    transaction.tx_json.SigningPubKey = '021FED5FD081CE5C4356431267D04C6E2167E4112C897D5E10335D4E22B4DA49ED';
    transaction.tx_json.Account = 'rMWwx3Ma16HnqSd4H6saPisihX9aKpXxHJ';
    transaction.tx_json.Flags = 0;
    transaction.tx_json.Fee = 10;
    transaction.tx_json.Sequence = 1;
    transaction.tx_json.TransactionType = 'AccountSet';

    assert.strictEqual(transaction.hash('HASH_TX_SIGN'), 'D1C15200CF532175F1890B6440AD223D3676140522BC11D2784E56760AE3B4FE');
    assert.strictEqual(transaction.hash('HASH_TX_SIGN_TESTNET'), '9FE7D27FC5B9891076B66591F99A683E01E0912986A629235459A3BD1961F341');

    done();
  });

  it('Get hash - invalid prefix', function(done) {
    const transaction = new Transaction();
    transaction._secret = 'sh2pTicynUEG46jjR4EoexHcQEoij';
    transaction.tx_json.SigningPubKey = '021FED5FD081CE5C4356431267D04C6E2167E4112C897D5E10335D4E22B4DA49ED';
    transaction.tx_json.Account = 'rMWwx3Ma16HnqSd4H6saPisihX9aKpXxHJ';
    transaction.tx_json.Flags = 0;
    transaction.tx_json.Fee = 10;
    transaction.tx_json.Sequence = 1;
    transaction.tx_json.TransactionType = 'AccountSet';

    assert.throws(function() {
      transaction.hash('HASH_TX_SIGNZ');
    });

    done();
  });

  it('Get hash - complex transaction', function() {
    const input_json = {
      Account: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
      Amount: {
        currency: 'LTC',
        issuer: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
        value: '9.985'
      },
      Destination: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
      Fee: '15',
      Flags: 0,
      Paths: [
        [
          {
            account: 'rMwjYedjc7qqtKYVLiAccJSmCwih4LnE2q',
            currency: 'USD',
            issuer: 'rMwjYedjc7qqtKYVLiAccJSmCwih4LnE2q',
            type: 49,
            type_hex: '0000000000000031'
          },
          {
            currency: 'LTC',
            issuer: 'rfYv1TXnwgDDK4WQNbFALykYuEBnrR4pDX',
            type: 48,
            type_hex: '0000000000000030'
          },
          {
            account: 'rfYv1TXnwgDDK4WQNbFALykYuEBnrR4pDX',
            currency: 'LTC',
            issuer: 'rfYv1TXnwgDDK4WQNbFALykYuEBnrR4pDX',
            type: 49,
            type_hex: '0000000000000031'
          }
        ]
      ],
      SendMax: {
        currency: 'USD',
        issuer: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
        value: '30.30993068'
      },
      Sequence: 415,
      SigningPubKey: '02854B06CE8F3E65323F89260E9E19B33DA3E01B30EA4CA172612DE77973FAC58A',
      TransactionType: 'Payment',
      TxnSignature: '304602210096C2F385530587DE573936CA51CB86B801A28F777C944E268212BE7341440B7F022100EBF0508A9145A56CDA7FAF314DF3BBE51C6EE450BA7E74D88516891A3608644E'
    };

    const expected_hash = '87366146D381AD971B97DD41CFAC1AE4670B0E996AB574B0CE18CE6467811868';
    const transaction = Transaction.from_json(input_json);

    assert.deepEqual(transaction.hash(), expected_hash);
  });

  it('Get hash - complex transaction including Memo', function() {
    const input_json = {
      Account: 'rfe8yiZUymRPx35BEwGjhfkaLmgNsTytxT',
      Amount: '1',
      Destination: 'r9kiSEUEw6iSCNksDVKf9k3AyxjW3r1qPf',
      Fee: '10',
      Flags: 2147483648,
      Memos: [
        {
          Memo: {
            MemoData: '61BAF845AF6D4FB1AC08A58D817261457034F1431FB9997B3460EB355DCA98D9382181A0E0125B4B0D6DAAF9A460D09C9EFDEFB2BE49E545036028A04DDFCFE8CBDD03DA844EEF9235B708574319A0F1186ADA054D2A4E970E73C67BE3232662726CD59C53CA2EF1DC0939C4793B1794932832D08B9B830AA917BB7CADA5B76C93DC5E0C928B93D8C336D6722E4757332A61DEB7E2A601E8D3766D5285A26A8DFAFFFDDED8BD49D471B9D885F3DF0CC031D522197BC248A5E2BEFAB12BC4A8D77BAB1555C8B38C26254C7BE8563D97EDD806EB7BE3872C750F28B41F693CB179849E6E2105B627F63D390FDDEFE863D3E7C28D6465AA158E7D96920E0EEF0BCB7993EC652C97F876F1666DBA9DA0A612FF9FE0A00AF03B2D5DADFC71984CBC93CA5EAE4C50BDDC839C1C6C3EEAAB44E8493BB7940C0C9ACA0BFEC2999DF5109A3EC40E62280E252712CC91476FB45E40EE314A26F3259027349E47FDD1C21A8DBDF58635943A13B7E2690B4CD153E2FA147A035E979BBDD814635BAB79683D7F62A7D7FF433F9DD35D0967F591D6D3776FC8ADF53E04EAB1DBC5863CBB85FC6F8E5C8B75037DEA9FEEB6A4D5FDF0AEE3F1BDD42EB1B976E98784A15C851E4F3B6234BAFFD11204CB2B76A3CBAA02E3B21051FAF012504DF33CAF9567A333DCE2BB5F454D4BA4B319DF43ECBC86DE214A712A4E214E874092DC84E05B',
            MemoType: 'D723898B3DB828F061BCE8DA8F3068B31E527CBDB4D0D22F3D4F5F2C6A961A84EF1189E9CBE2741FEC5C7A46011316D6F9A7769C8E8157E5209FED2D3F950E2763BEF07254327B0EDE9C3CFEB248997EDF148BA36E9D1167C87D73FE9F047FF167DF37B0EF30ABF8E5FD0DCC7E7B964EA0E60E8B2C27E2C7C214BD8334CE830B66BBD724172F7ADCEF491B9B495A979819944DF8EB3A13E4F03B2A8D6FFF332E5C9980A540BEDF659DFDDA03EC78F4F0279F90C8BFD494C15708197C2BAE5CA661EC75FB6E6097E7C5435F374332B7A066FBBC14C629E8EE6042A64226B075B9309BF5FE227CEEEB9CEC7A6B79E724BFBA2BA706F28EC9F3702FB3AFB4C74F0411C7EFDE7927D1ED0670C4F426B8C40F09EB715713788902A4374D8CC7E5111BCA39A97D1F9BD3BD56E28E6167E4DC97A7DEF5428B809D03AE72CB5BA1D25DE3523BC182E3B8905666A972A949B20C30C4FBD1D0A2D9AD8E46EFDBE4E46F4E340FE39F4AD315F5D9EBF7ED9BDC6D577375B56E0CD9FAF0BFA02453F90290E0962D6362048F737BEE3E0E1C46ECE61CCAF4C317B4135B2C5B5D5C4EE728002B2116BB1AF21903AB3F2E4E1A4FE4C5D76507C71C50670281DAB334C37503FB851FC25EC85C757976450004EC642E217D7F4B2E4B6DD820B5E3968B79CB9D7706F28714003C63F4B89AD1B6208A56DF5AFF02E5327A8EAA532BD3ED1ED0'
          }
        }
      ],
      Sequence: 74519,
      SigningPubKey: '02BA5A9F27C34830542D7AA460B82D68AB34B410470108EE2DFFD9D280B49DB161',
      TransactionType: 'Payment',
      TxnSignature: '3045022100EA99CD20B47AB1C7AEF348102B515DB2FA26F1C9E7DC8FCAE72A763CEC37F21102206190F16F509A088E6303ACB66E9A6C1B9886C20B23F55C3B0721F2B97DC0926E',
      date: 455562850,
      hash: '4907745B5254B1093E037BA3250B95CFAF35C11CC2CA5E538A68FCE39D16F402',
      inLedger: 7085699,
      ledger_index: 7085699,
      meta: {
        AffectedNodes: [
          {
            ModifiedNode: {
              FinalFields: {
                Account: 'r9kiSEUEw6iSCNksDVKf9k3AyxjW3r1qPf',
                Balance: '40900380802',
                Flags: 0,
                OwnerCount: 6,
                Sequence: 98
              },
              LedgerEntryType: 'AccountRoot',
              LedgerIndex: '0EB37649FDF753A78DACB689057E827296F47AB7D04A7CB273C971A207E17960',
              PreviousFields: {
                Balance: '40900380801'
              },
              PreviousTxnID: '42C003842B5188E860B087DD509880C82263B31DE3DD6B5E4AC89AF541CF486E',
              PreviousTxnLgrSeq: 5088491
            }
          },
          {
            ModifiedNode: {
              FinalFields: {
                Account: 'rfe8yiZUymRPx35BEwGjhfkaLmgNsTytxT',
                Balance: '35339588',
                Domain: '9888A596C489373CF5C40858A66C8922C54A2BE9521E39CFFD0EF7A9906DAF298CDA4ADA16111A020C8B940CBABC4D39406381E0DE791147FAD0A5729F3546BF47D515FCDC80A85A52701CF9120C64DD6D41D0CF3DF2B56AEF6DBB463BB69F153BEEAC31D5300B8A3AE558122E192D7211DC0E4F547AD96B2E2F30F46AF8B5D76A58A75D764BA6FDC8E748EEC7A29C2F2A71784B8141D1A9E66544FF7C07025827C3BBCD66FA121D5E50407A622C803B33FCFBA4B2A7454BF86C32628DE0259EC0014783871BD3ADAF2E9F4E0FA421A68AFE1EF3ADEDD9CB24E783D284666BA8ABC2428F77D5550BE76751AA500A90E648CF7524CFC8E8785CB1ACBFB5F0AA50',
                EmailHash: 'A9527827303A62DA5DB83FE47ACC1B62',
                Flags: 1048576,
                OwnerCount: 0,
                RegularKey: 'rDLNuE5hfxbRzuXaNn7iUQBftoKfYQQtFA',
                Sequence: 74520,
                WalletLocator: 'D091F672A5A6FCE5AD3CD35BDD7E6FA15D4205D22E5EB36CBFB961D6E355EF9A'
              },
              LedgerEntryType: 'AccountRoot',
              LedgerIndex: 'A85F8FD83E579C50AF595482824E2AC8C747E4673E83537E8CACCE595AA0C590',
              PreviousFields: {
                Balance: '35339599',
                Sequence: 74519
              },
              PreviousTxnID: '3CB97C94E76F3A26C083DCA702E91446EB51D4DE62B25D8B7BA10F24FDA2F4E2',
              PreviousTxnLgrSeq: 7085699
            }
          }
        ],
        TransactionIndex: 13,
        TransactionResult: 'tesSUCCESS',
        delivered_amount: '1'
      },
      validated: true
    };

    assert.deepEqual(Transaction.from_json(input_json).hash(), input_json.hash);
  });

  it('Serialize transaction', function() {
    const input_json = {
      Account: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
      Amount: {
        currency: 'LTC',
        issuer: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
        value: '9.985'
      },
      Destination: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
      Fee: '15',
      Flags: 0,
      Paths: [
        [
          {
            account: 'rMwjYedjc7qqtKYVLiAccJSmCwih4LnE2q',
            currency: 'USD',
            issuer: 'rMwjYedjc7qqtKYVLiAccJSmCwih4LnE2q',
            type: 49,
            type_hex: '0000000000000031'
          },
          {
            currency: 'LTC',
            issuer: 'rfYv1TXnwgDDK4WQNbFALykYuEBnrR4pDX',
            type: 48,
            type_hex: '0000000000000030'
          },
          {
            account: 'rfYv1TXnwgDDK4WQNbFALykYuEBnrR4pDX',
            currency: 'LTC',
            issuer: 'rfYv1TXnwgDDK4WQNbFALykYuEBnrR4pDX',
            type: 49,
            type_hex: '0000000000000031'
          }
        ]
      ],
      SendMax: {
        currency: 'USD',
        issuer: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
        value: '30.30993068'
      },
      Sequence: 415,
      SigningPubKey: '02854B06CE8F3E65323F89260E9E19B33DA3E01B30EA4CA172612DE77973FAC58A',
      TransactionType: 'Payment',
      TxnSignature: '304602210096C2F385530587DE573936CA51CB86B801A28F777C944E268212BE7341440B7F022100EBF0508A9145A56CDA7FAF314DF3BBE51C6EE450BA7E74D88516891A3608644E'
    };

    const expected_hex = '1200002200000000240000019F61D4A3794DFA1510000000000000000000000000004C54430000000000EF7ED76B77750D79EC92A59389952E0E8054407668400000000000000F69D4CAC4AC112283000000000000000000000000005553440000000000EF7ED76B77750D79EC92A59389952E0E80544076732102854B06CE8F3E65323F89260E9E19B33DA3E01B30EA4CA172612DE77973FAC58A7448304602210096C2F385530587DE573936CA51CB86B801A28F777C944E268212BE7341440B7F022100EBF0508A9145A56CDA7FAF314DF3BBE51C6EE450BA7E74D88516891A3608644E8114EF7ED76B77750D79EC92A59389952E0E805440768314EF7ED76B77750D79EC92A59389952E0E80544076011231DD39C650A96EDA48334E70CC4A85B8B2E8502CD30000000000000000000000005553440000000000DD39C650A96EDA48334E70CC4A85B8B2E8502CD3300000000000000000000000004C5443000000000047DA9E2E00ECF224A52329793F1BB20FB1B5EA643147DA9E2E00ECF224A52329793F1BB20FB1B5EA640000000000000000000000004C5443000000000047DA9E2E00ECF224A52329793F1BB20FB1B5EA6400';

    const transaction = Transaction.from_json(input_json);

    assert.deepEqual(transaction.serialize().to_hex(), expected_hex);
  });

  it('Sign transaction', function(done) {
    const transaction = new Transaction();
    transaction._secret = 'sh2pTicynUEG46jjR4EoexHcQEoij';
    transaction.tx_json.SigningPubKey = '021FED5FD081CE5C4356431267D04C6E2167E4112C897D5E10335D4E22B4DA49ED';
    transaction.tx_json.Account = 'rMWwx3Ma16HnqSd4H6saPisihX9aKpXxHJ';
    transaction.tx_json.Flags = 0;
    transaction.tx_json.Fee = 10;
    transaction.tx_json.Sequence = 1;
    transaction.tx_json.TransactionType = 'AccountSet';

    transaction.sign();

    const signature = transaction.tx_json.TxnSignature;

    assert.strictEqual(transaction.previousSigningHash, 'D1C15200CF532175F1890B6440AD223D3676140522BC11D2784E56760AE3B4FE');
    assert(/^[A-Z0-9]+$/.test(signature));

    // Unchanged transaction, signature should be the same
    transaction.sign();

    assert.strictEqual(transaction.previousSigningHash, 'D1C15200CF532175F1890B6440AD223D3676140522BC11D2784E56760AE3B4FE');
    assert(/^[A-Z0-9]+$/.test(signature));
    assert.strictEqual(transaction.tx_json.TxnSignature, signature);

    done();
  });

  it('Add transaction ID', function(done) {
    const transaction = new Transaction();

    transaction.addId('D1C15200CF532175F1890B6440AD223D3676140522BC11D2784E56760AE3B4FE');
    transaction.addId('F1C15200CF532175F1890B6440AD223D3676140522BC11D2784E56760AE3B4FE');
    transaction.addId('F1C15200CF532175F1890B6440AD223D3676140522BC11D2784E56760AE3B4FE');

    assert.deepEqual(
      transaction.submittedIDs,
      ['F1C15200CF532175F1890B6440AD223D3676140522BC11D2784E56760AE3B4FE',
        'D1C15200CF532175F1890B6440AD223D3676140522BC11D2784E56760AE3B4FE']
    );

    done();
  });

  it('Find transaction IDs in cache', function(done) {
    const transaction = new Transaction();

    assert.deepEqual(transaction.findId({
      F1C15200CF532175F1890B6440AD223D3676140522BC11D2784E56760AE3B4FE: transaction
    }), undefined);

    transaction.addId('F1C15200CF532175F1890B6440AD223D3676140522BC11D2784E56760AE3B4FE');

    assert.deepEqual(transaction.findId({
      F1C15200CF532175F1890B6440AD223D3676140522BC11D2784E56760AE3B4FE: transaction
    }), transaction);

    assert.strictEqual(transaction.findId({
      Z1C15200CF532175F1890B6440AD223D3676140522BC11D2784E56760AE3B4FE: transaction
    }), undefined);

    done();
  });

  it('Set build_path', function() {
    const transaction = new Transaction();

//    assert.throws(function() {
//      transaction.buildPath();
//    }, /Error: TransactionType must be Payment to use build_path/);
//    assert.throws(function() {
//      transaction.setBuildPath();
//    }, /Error: TransactionType must be Payment to use build_path/);

    assert.strictEqual(transaction._build_path, false);

    transaction.setType('Payment');
    transaction.setBuildPath();
    assert.strictEqual(transaction._build_path, true);
    transaction.setBuildPath(false);
    assert.strictEqual(transaction._build_path, false);
  });

  it('Set DestinationTag', function() {
    const transaction = new Transaction();

//     assert.throws(function() {
//       transaction.destinationTag(1);
//     }, /Error: TransactionType must be Payment to use DestinationTag/);
//     assert.throws(function() {
//       transaction.setDestinationTag(1);
//     }, /Error: TransactionType must be Payment to use DestinationTag/);

    assert.strictEqual(transaction.tx_json.DestinationTag, undefined);

    transaction.setType('Payment');

    assert.throws(function() {
      transaction.setDestinationTag('tag');
    }, /Error: DestinationTag must be a valid UInt32/);

    transaction.setDestinationTag(1);
    assert.strictEqual(transaction.tx_json.DestinationTag, 1);
  });

  it('Set InvoiceID', function() {
    const transaction = new Transaction();

//     assert.throws(function() {
//       transaction.invoiceID('DEADBEEF');
//     }, /Error: TransactionType must be Payment to use InvoiceID/);
//     assert.strictEqual(transaction.tx_json.InvoiceID, undefined);

    transaction.setType('Payment');

    assert.throws(function() {
      transaction.setInvoiceID(1);
    }, /Error: InvoiceID must be a valid Hash256/);

    assert.strictEqual(transaction.tx_json.InvoiceID, undefined);

    transaction.setType('Payment');

    transaction.setInvoiceID('DEADBEEF');
    assert.strictEqual(transaction.tx_json.InvoiceID, 'DEADBEEF00000000000000000000000000000000000000000000000000000000');

    transaction.setInvoiceID('FEADBEEF00000000000000000000000000000000000000000000000000000000');
    assert.strictEqual(transaction.tx_json.InvoiceID, 'FEADBEEF00000000000000000000000000000000000000000000000000000000');
  });

  it('Set ClientID', function() {
    const transaction = new Transaction();

    transaction.clientID(1);
    assert.strictEqual(transaction._clientID, undefined);

    transaction.clientID('DEADBEEF');
    assert.strictEqual(transaction._clientID, 'DEADBEEF');
  });

  it('Set LastLedgerSequence', function() {
    const transaction = new Transaction();

    assert.throws(function() {
      transaction.lastLedger('a');
    }, /Error: LastLedgerSequence must be a valid UInt32/);
    assert.throws(function() {
      transaction.setLastLedgerSequence('a');
    }, /Error: LastLedgerSequence must be a valid UInt32/);
    assert.throws(function() {
      transaction.setLastLedgerSequence(NaN);
    }, /Error: LastLedgerSequence must be a valid UInt32/);

    assert.strictEqual(transaction.tx_json.LastLedgerSequence, undefined);
    assert(!transaction._setLastLedger);

    transaction.setLastLedgerSequence(12);
    assert.strictEqual(transaction.tx_json.LastLedgerSequence, 12);
    assert(transaction._setLastLedger);
  });

  it('Set Max Fee', function() {
    const transaction = new Transaction();

    transaction.maxFee('a');
    assert(!transaction._setMaxFee);

    transaction.maxFee(NaN);
    assert(!transaction._setMaxFee);

    transaction.maxFee(1000);
    assert.strictEqual(transaction._maxFee, 1000);
    assert.strictEqual(transaction._setMaxFee, true);
  });

  it('Set Fixed Fee', function() {
    const transaction = new Transaction();

    transaction.setFixedFee('a');
    assert(!transaction._setFixedFee);

    transaction.setFixedFee(-1000);
    assert(!transaction._setFixedFee);

    transaction.setFixedFee(NaN);
    assert(!transaction._setFixedFee);

    transaction.setFixedFee(1000);
    assert.strictEqual(transaction._setFixedFee, true);
    assert.strictEqual(transaction.tx_json.Fee, '1000');
  });

  it('Set resubmittable', function() {
    const tx = new Transaction();

    assert.strictEqual(tx.isResubmittable(), true);

    tx.setResubmittable(false);
    assert.strictEqual(tx.isResubmittable(), false);

    tx.setResubmittable(true);
    assert.strictEqual(tx.isResubmittable(), true);
  });

  it('Rewrite transaction path', function() {
    const path = [
      {
        account: 'rP51ycDJw5ZhgvdKiRjBYZKYjsyoCcHmnY',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
        extraneous_property: 1,
        currency: 'USD'
      },
      {
        account: 'rP51ycDJw5ZhgvdKiRjBYZKYjsyoCcHmnY',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
        type_hex: '0000000000000001'
      },
      {
        account: 'rP51ycDJw5ZhgvdKiRjBYZKYjsyoCcHmnY',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
        test: 'XRP',
        currency: 'USD'
      }
    ];

    assert.deepEqual(Transaction._rewritePath(path), [
      {
        account: 'rP51ycDJw5ZhgvdKiRjBYZKYjsyoCcHmnY',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
        currency: 'USD'
      },
      {
        account: 'rP51ycDJw5ZhgvdKiRjBYZKYjsyoCcHmnY',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
        type_hex: '0000000000000001'
      },
      {
        account: 'rP51ycDJw5ZhgvdKiRjBYZKYjsyoCcHmnY',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
        currency: 'USD'
      }
    ]);
  });

  it('Rewrite transaction path - invalid path', function() {
    assert.throws(function() {
      assert.strictEqual(Transaction._rewritePath(1), undefined);
    });
  });

  it('Add transaction path', function() {
    const transaction = new Transaction();

    const path = [
      {
        account: 'rP51ycDJw5ZhgvdKiRjBYZKYjsyoCcHmnY',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
        extraneous_property: 1,
        currency: 'USD'
      }
    ];

    const path2 = [
      {
        account: 'rP51ycDJw5ZhgvdKiRjBYZKYjsyoCcHmnY',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
        test: 'XRP',
        currency: 'USD'
      }
    ];

//     assert.throws(function() {
//       transaction.pathAdd(path);
//     }, /^Error: TransactionType must be Payment to use Paths$/);
//     assert.throws(function() {
//       transaction.addPath(path);
//     }, /^Error: TransactionType must be Payment to use Paths$/);

    assert.strictEqual(transaction.tx_json.Paths, undefined);

    transaction.setType('Payment');

    assert.throws(function() {
      transaction.pathAdd(1);
    }, /^Error: Path must be an array$/);
    assert.throws(function() {
      transaction.addPath(1);
    }, /^Error: Path must be an array$/);

    transaction.addPath(path);

    assert.deepEqual(transaction.tx_json.Paths, [
      [{
        account: 'rP51ycDJw5ZhgvdKiRjBYZKYjsyoCcHmnY',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
        currency: 'USD'
      }]
    ]);

    transaction.addPath(path2);

    assert.deepEqual(transaction.tx_json.Paths, [
      [{
        account: 'rP51ycDJw5ZhgvdKiRjBYZKYjsyoCcHmnY',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
        currency: 'USD'
      }],
      [{
        account: 'rP51ycDJw5ZhgvdKiRjBYZKYjsyoCcHmnY',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
        currency: 'USD'
      }]
    ]);
  });

  it('Add transaction paths', function() {
    const transaction = new Transaction();

    const paths = [
      [{
        account: 'rP51ycDJw5ZhgvdKiRjBYZKYjsyoCcHmnY',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
        test: 1,
        currency: 'USD'
      }],
      [{
        account: 'rP51ycDJw5ZhgvdKiRjBYZKYjsyoCcHmnY',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
        test: 2,
        currency: 'USD'
      }]
    ];

//     assert.throws(function() {
//       transaction.paths(paths);
//     }, /Error: TransactionType must be Payment to use Paths/);
//     assert.throws(function() {
//       transaction.setPaths(paths);
//     }, /Error: TransactionType must be Payment to use Paths/);

    assert.strictEqual(transaction.tx_json.Paths, undefined);

    transaction.setType('Payment');

    assert.throws(function() {
      transaction.paths(1);
    }, /Error: Paths must be an array/);
    assert.throws(function() {
      transaction.setPaths(1);
    }, /Error: Paths must be an array/);

    transaction.setPaths(paths);
    transaction.setPaths(paths);

    assert.deepEqual(transaction.tx_json.Paths, [
      [{
        account: 'rP51ycDJw5ZhgvdKiRjBYZKYjsyoCcHmnY',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
        currency: 'USD'
      }],
      [{
        account: 'rP51ycDJw5ZhgvdKiRjBYZKYjsyoCcHmnY',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
        currency: 'USD'
      }]
    ]);
  });

  it('Does not add empty transaction paths', function() {
    const transaction = new Transaction();

    const paths = [];

    assert.strictEqual(transaction.tx_json.Paths, undefined);

    transaction.setType('Payment');

    assert.throws(function() {
      transaction.paths(1);
    }, /Error: Paths must be an array/);
    assert.throws(function() {
      transaction.setPaths(1);
    }, /Error: Paths must be an array/);

    transaction.setPaths(paths);

    assert.strictEqual(transaction.tx_json.Paths, undefined);
  });

  it('Set secret', function() {
    const transaction = new Transaction();
    transaction.secret('shHXjwp9m3MDQNcUrTekXcdzFsCjM');
    assert.strictEqual(transaction._secret, 'shHXjwp9m3MDQNcUrTekXcdzFsCjM');
  });

  it('Set SendMax', function() {
    const transaction = new Transaction();

//     assert.throws(function() {
//       transaction.sendMax('1/USD/rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm');
//     }, /^Error: TransactionType must be Payment to use SendMax$/);
//     assert.throws(function() {
//       transaction.setSendMax('1/USD/rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm');
//     }, /^Error: TransactionType must be Payment to use SendMax$/);

    transaction.setType('Payment');
    transaction.setSendMax('1/USD/rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm');

    assert.deepEqual(transaction.tx_json.SendMax, {
      value: '1',
      currency: 'USD',
      issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm'
    });
  });

  it('Set SourceTag', function() {
    const transaction = new Transaction();

    assert.throws(function() {
      transaction.sourceTag('tag');
    }, /Error: SourceTag must be a valid UInt32/);

    assert.strictEqual(transaction.tx_json.SourceTag, undefined);
    transaction.sourceTag(1);
    assert.strictEqual(transaction.tx_json.SourceTag, 1);
    transaction.setSourceTag(2);
    assert.strictEqual(transaction.tx_json.SourceTag, 2);
  });

  it('Set TransferRate', function() {
    const transaction = new Transaction();
    transaction.setType('AccountSet');

    assert.throws(function() {
      transaction.transferRate(1);
    }, /^Error: TransferRate must be >= 1000000000$/);
    assert.throws(function() {
      transaction.setTransferRate(1);
    }, /^Error: TransferRate must be >= 1000000000$/);
    assert.throws(function() {
      transaction.setTransferRate('a');
    }, /^Error: TransferRate must be a valid UInt32$/);

    assert.strictEqual(transaction.tx_json.TransferRate, undefined);
    transaction.setTransferRate(1.5 * 1e9);
    assert.strictEqual(transaction.tx_json.TransferRate, 1.5 * 1e9);
    transaction.setTransferRate(0);
    assert.strictEqual(transaction.tx_json.TransferRate, 0);
  });

  it('Set Flags', function(done) {
    const transaction = new Transaction();
    transaction.tx_json.TransactionType = 'Payment';
    transaction.setFlags();
    assert.strictEqual(transaction.tx_json.Flags, 0);

    const transaction2 = new Transaction();
    transaction2.tx_json.TransactionType = 'Payment';
    transaction2.setFlags(Transaction.flags.Payment.PartialPayment);
    assert.strictEqual(transaction2.tx_json.Flags, 131072);

    const transaction3 = new Transaction();
    transaction3.tx_json.TransactionType = 'Payment';
    transaction3.setFlags('NoRippleDirect');
    assert.strictEqual(transaction3.tx_json.Flags, 65536);

    const transaction4 = new Transaction();
    transaction4.tx_json.TransactionType = 'Payment';
    transaction4.setFlags('PartialPayment', 'NoRippleDirect');
    assert.strictEqual(transaction4.tx_json.Flags, 196608);

    const transaction5 = new Transaction();
    transaction5.setType('Payment');
    transaction5.setFlags(['LimitQuality', 'PartialPayment']);
    assert.strictEqual(transaction5.tx_json.Flags, 393216);

    const transaction6 = new Transaction();
    transaction6.tx_json.TransactionType = 'Payment';
    transaction6.once('error', function(err) {
      assert.strictEqual(err.result, 'tejInvalidFlag');
      done();
    });
    transaction6.setFlags('asdf');
  });

  it('Add Memo', function() {
    const transaction = new Transaction();
    transaction.tx_json.TransactionType = 'Payment';

    const memoType = 'message';
    const memoFormat = 'json';
    const memoData = {
      string: 'value',
      bool: true,
      integer: 1
    };

    transaction.addMemo(memoType, memoFormat, memoData);

    const expected = [
      {
        Memo:
          {
            MemoType: '6D657373616765',
            MemoFormat: '6A736F6E',
            MemoData: '7B22737472696E67223A2276616C7565222C22626F6F6C223A747275652C22696E7465676572223A317D'
          }
      }
    ];

    assert.deepEqual(transaction.tx_json.Memos, expected);

    allowed_memo_chars.forEach(function(c) {
      const hexStr = new Buffer(c).toString('hex').toUpperCase();
      const tx = new Transaction();

      tx.addMemo(c, c, c);

      assert.deepEqual(tx.tx_json.Memos, [{
        Memo: {
          MemoType: hexStr,
          MemoFormat: hexStr,
          MemoData: hexStr
        }
      }]);
    });
  });

  it('Add Memo - by object', function() {
    const transaction = new Transaction();
    transaction.setType('Payment');

    const memo = {
      memoType: 'type',
      memoData: 'data'
    };

    transaction.addMemo(memo);

    const expected = [
      {
        Memo: {
          MemoType: '74797065',
          MemoData: '64617461'
        }
      }
    ];

    assert.deepEqual(transaction.tx_json.Memos, expected);
  });

  it('Add Memos', function() {
    const transaction = new Transaction();
    transaction.tx_json.TransactionType = 'Payment';

    transaction.addMemo('testkey', undefined, 'testvalue');
    transaction.addMemo('testkey2', undefined, 'testvalue2');
    transaction.addMemo('testkey3', 'text/html');
    transaction.addMemo(undefined, undefined, 'testvalue4');
    transaction.addMemo('testkey4', 'text/html', '<html>');

    const expected = [
      {
        Memo: {
          MemoType: '746573746B6579',
          MemoData: '7465737476616C7565'
        }
      },
      {
        Memo: {
          MemoType: '746573746B657932',
          MemoData: '7465737476616C756532'
        }
      },
      {
        Memo: {
          MemoType: '746573746B657933',
          MemoFormat: '746578742F68746D6C'
        }
      },
      {
        Memo: {
          MemoData: '7465737476616C756534'
        }
      },
      {
        Memo: {
          MemoType: '746573746B657934',
          MemoFormat: '746578742F68746D6C',
          MemoData: '3C68746D6C3E'
        }
      }
    ];

    assert.deepEqual(transaction.tx_json.Memos, expected);
  });

  it('Add Memo - invalid MemoType', function() {
    const transaction = new Transaction();
    transaction.tx_json.TransactionType = 'Payment';

    const error_regex = /^Error: MemoType must be a string containing only valid URL characters$/;

    assert.throws(function() {
      transaction.addMemo(1);
    }, error_regex);
    assert.throws(function() {
      transaction.addMemo('한국어');
    }, error_regex);
    assert.throws(function() {
      transaction.addMemo('my memo');
    }, error_regex);

    disallowed_memo_chars.forEach(function(c) {
      assert.throws(function() {
        transaction.addMemo(c);
      }, error_regex);
    });
  });

  it('Add Memo - invalid MemoFormat', function() {
    const transaction = new Transaction();
    transaction.tx_json.TransactionType = 'Payment';

    const error_regex = /^Error: MemoFormat must be a string containing only valid URL characters$/;

    assert.throws(function() {
      transaction.addMemo(undefined, 1);
    }, error_regex);
    assert.throws(function() {
      transaction.addMemo(undefined, 'России');
    }, error_regex);
    assert.throws(function() {
      transaction.addMemo(undefined, 'my memo');
    }, error_regex);

    disallowed_memo_chars.forEach(function(c) {
      assert.throws(function() {
        transaction.addMemo(undefined, c);
      }, error_regex);
    });
  });

  it('Add Memo - MemoData string', function() {
    const transaction = new Transaction();
    transaction.tx_json.TransactionType = 'Payment';

    transaction.addMemo({memoData: 'some_string'});

    assert.deepEqual(transaction.tx_json.Memos, [
      {
        Memo: {
          MemoData: '736F6D655F737472696E67'
        }
      }
    ]);
  });

  it('Add Memo - MemoData complex object', function() {
    const transaction = new Transaction();
    transaction.tx_json.TransactionType = 'Payment';

    const memo = {
      memoFormat: 'json',
      memoData: {
        string: 'string',
        int: 1,
        array: [
          {
            string: 'string'
          }
        ],
        object: {
          string: 'string'
        }
      }
    };

    transaction.addMemo(memo);

    assert.deepEqual(transaction.tx_json.Memos, [
      {
        Memo: {
          MemoFormat: '6A736F6E',
          MemoData: '7B22737472696E67223A22737472696E67222C22696E74223A312C226172726179223A5B7B22737472696E67223A22737472696E67227D5D2C226F626A656374223A7B22737472696E67223A22737472696E67227D7D'
        }
      }
    ]);
  });

  it('Set AccountTxnID', function() {
    const transaction = new Transaction();
    assert(transaction instanceof Transaction);

    transaction.accountTxnID('75C5A92212AA82A89C3824F6F071FE49C95C45DE9113EB51763A217DBACB5B4F');

    assert.deepEqual(transaction.tx_json, {
      Flags: 0,
      AccountTxnID: '75C5A92212AA82A89C3824F6F071FE49C95C45DE9113EB51763A217DBACB5B4F'
    });
  });

  it('Construct AccountSet transaction - with setFlag, clearFlag', function() {
    const transaction = new Transaction().accountSet('rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm', 'asfRequireDest', 'asfRequireAuth');

    assert(transaction instanceof Transaction);
    assert.deepEqual(transaction.tx_json, {
      Flags: 0,
      SetFlag: 1,
      ClearFlag: 2,
      TransactionType: 'AccountSet',
      Account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm'
    });
  });

  it('Construct AccountSet transaction - with AccountTxnID SetFlag', function() {
    const transaction = new Transaction().accountSet('rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm', 'asfAccountTxnID');

    assert(transaction instanceof Transaction);
    assert.deepEqual(transaction.tx_json, {
      Flags: 0,
      SetFlag: 5,
      TransactionType: 'AccountSet',
      Account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm'
    });
  });

  it('Construct AccountSet transaction - params object', function() {
    const transaction = new Transaction().accountSet({
      account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      set: 'asfRequireDest',
      clear: 'asfRequireAuth'
    });

    assert(transaction instanceof Transaction);
    assert.deepEqual(transaction.tx_json, {
      Flags: 0,
      SetFlag: 1,
      ClearFlag: 2,
      TransactionType: 'AccountSet',
      Account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm'
    });
  });

  it('Construct AccountSet transaction - invalid account', function() {
    assert.throws(function() {
      new Transaction().accountSet('xrsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm');
    });
  });

  it('Construct OfferCancel transaction', function() {
    const transaction = new Transaction().offerCancel('rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm', 1);

    assert(transaction instanceof Transaction);
    assert.deepEqual(transaction.tx_json, {
      Flags: 0,
      TransactionType: 'OfferCancel',
      Account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      OfferSequence: 1
    });
  });

  it('Construct OfferCancel transaction - params object', function() {
    const transaction = new Transaction().offerCancel({
      account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      sequence: 1
    });

    assert(transaction instanceof Transaction);
    assert.deepEqual(transaction.tx_json, {
      Flags: 0,
      TransactionType: 'OfferCancel',
      Account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      OfferSequence: 1
    });
  });

  it('Construct OfferCancel transaction - invalid account', function() {
    assert.throws(function() {
      new Transaction().offerCancel('xrsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm', 1);
    });
  });

  it('Construct OfferCreate transaction', function() {
    const bid = '1/USD/rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm';
    const ask = '1/EUR/rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm';

    const transaction = new Transaction().offerCreate('rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm', bid, ask);

    assert(transaction instanceof Transaction);
    assert.deepEqual(transaction.tx_json, {
      Flags: 0,
      TransactionType: 'OfferCreate',
      Account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      TakerPays: {
        value: '1',
        currency: 'USD',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm'
      },
      TakerGets: {
        value: '1',
        currency: 'EUR',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm'
      }
    });
  });

  it('Construct OfferCreate transaction - with expiration, cancelSequence', function() {
    const bid = '1/USD/rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm';
    const ask = '1/EUR/rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm';
    const expiration = new Date();
    expiration.setHours(expiration.getHours() + 1);

    const rippleExpiration = Math.round(expiration.getTime() / 1000) - 0x386D4380;

    const transaction = new Transaction().offerCreate('rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm', bid, ask, expiration, 1);

    assert(transaction instanceof Transaction);
    assert.deepEqual(transaction.tx_json, {
      Flags: 0,
      TransactionType: 'OfferCreate',
      Account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      TakerPays: {
        value: '1',
        currency: 'USD',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm'
      },
      TakerGets: {
        value: '1',
        currency: 'EUR',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm'
      },
      Expiration: rippleExpiration,
      OfferSequence: 1
    });
  });

  it('Construct OfferCreate transaction - params object', function() {
    const bid = '1/USD/rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm';
    const ask = '1/EUR/rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm';
    const expiration = new Date();
    expiration.setHours(expiration.getHours() + 1);

    const rippleExpiration = Math.round(expiration.getTime() / 1000) - 0x386D4380;

    const transaction = new Transaction().offerCreate({
      account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      taker_gets: ask,
      taker_pays: bid,
      expiration: expiration,
      cancel_sequence: 1
    });

    assert(transaction instanceof Transaction);
    assert.deepEqual(transaction.tx_json, {
      Flags: 0,
      TransactionType: 'OfferCreate',
      Account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      TakerPays: {
        value: '1',
        currency: 'USD',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm'
      },
      TakerGets: {
        value: '1',
        currency: 'EUR',
        issuer: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm'
      },
      Expiration: rippleExpiration,
      OfferSequence: 1
    });
  });

  it('Construct OfferCreate transaction - invalid account', function() {
    const bid = '1/USD/rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm';
    const ask = '1/EUR/rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm';
    assert.throws(function() {
      new Transaction().offerCreate('xrsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm', bid, ask);
    });
  });

  it('Construct SetRegularKey transaction', function() {
    const transaction = new Transaction().setRegularKey('rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm', 'r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe');

    assert(transaction instanceof Transaction);
    assert.deepEqual(transaction.tx_json, {
      Flags: 0,
      TransactionType: 'SetRegularKey',
      Account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      RegularKey: 'r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe'
    });
  });

  it('Construct SetRegularKey transaction - params object', function() {
    const transaction = new Transaction().setRegularKey({
      account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      regular_key: 'r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe'
    });

    assert(transaction instanceof Transaction);
    assert.deepEqual(transaction.tx_json, {
      Flags: 0,
      TransactionType: 'SetRegularKey',
      Account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      RegularKey: 'r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe'
    });
  });

  it('Construct SetRegularKey transaction - invalid account', function() {
    assert.throws(function() {
      new Transaction().setRegularKey('xrsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm', 'r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe');
    });
  });

  it('Construct SetRegularKey transaction - invalid regularKey', function() {
    assert.throws(function() {
      new Transaction().setRegularKey('rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm', 'xr36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe');
    });
  });

  it('Construct Payment transaction', function() {
    const transaction = new Transaction().payment(
      'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      'r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe',
      '1'
    );

    assert(transaction instanceof Transaction);
    assert.deepEqual(transaction.tx_json, {
      Flags: 0,
      TransactionType: 'Payment',
      Account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      Destination: 'r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe',
      Amount: '1'
    });
  });

  it('Construct Payment transaction - complex amount', function() {
    const transaction = new Transaction().payment(
      'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      'r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe',
      '1/USD/r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe'
    );

    assert(transaction instanceof Transaction);
    assert.deepEqual(transaction.tx_json, {
      Flags: 0,
      TransactionType: 'Payment',
      Account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      Destination: 'r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe',
      Amount: {
        value: '1',
        currency: 'USD',
        issuer: 'r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe'
      }
    });
  });

  it('Construct Payment transaction - params object', function() {
    const transaction = new Transaction().payment({
      account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      destination: 'r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe',
      amount: '1/USD/r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe'
    });

    assert(transaction instanceof Transaction);
    assert.deepEqual(transaction.tx_json, {
      Flags: 0,
      TransactionType: 'Payment',
      Account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      Destination: 'r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe',
      Amount: {
        value: '1',
        currency: 'USD',
        issuer: 'r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe'
      }
    });
  });

  it('Construct Payment transaction - invalid account', function() {
    assert.throws(function() {
      new Transaction().payment(
        'xrsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
        'r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe',
        '1/USD/r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe'
      );
    });
  });

  it('Construct Payment transaction - invalid destination', function() {
    assert.throws(function() {
      new Transaction().payment(
        'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
        'xr36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe',
        '1/USD/r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe'
      );
    });
  });

  it('Construct TrustSet transaction', function() {
    const limit = '1/USD/r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe';
    const transaction = new Transaction().trustSet('rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm', limit);
    assert(transaction instanceof Transaction);
    assert.deepEqual(transaction.tx_json, {
      Flags: 0,
      TransactionType: 'TrustSet',
      Account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      LimitAmount: {
        value: '1',
        currency: 'USD',
        issuer: 'r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe'
      }
    });
  });

  it('Construct TrustSet transaction - with qualityIn, qualityOut', function() {
    const limit = '1/USD/r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe';
    const transaction = new Transaction().trustSet('rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm', limit, 1.0, 1.0);
    assert(transaction instanceof Transaction);
    assert.deepEqual(transaction.tx_json, {
      Flags: 0,
      TransactionType: 'TrustSet',
      Account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      LimitAmount: {
        value: '1',
        currency: 'USD',
        issuer: 'r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe'
      },
      QualityIn: 1.0,
      QualityOut: 1.0
    });
  });

  it('Construct TrustSet transaction - params object', function() {
    const limit = '1/USD/r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe';
    const transaction = new Transaction().trustSet({
      account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      limit: limit,
      quality_in: 1.0,
      quality_out: 1.0
    });

    assert(transaction instanceof Transaction);
    assert.deepEqual(transaction.tx_json, {
      Flags: 0,
      TransactionType: 'TrustSet',
      Account: 'rsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm',
      LimitAmount: {
        value: '1',
        currency: 'USD',
        issuer: 'r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe'
      },
      QualityIn: 1.0,
      QualityOut: 1.0
    });
  });

  it('Construct TrustSet transaction - invalid account', function() {
    assert.throws(function() {
      const limit = '1/USD/r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe';
      new Transaction().trustSet('xrsLEU1TPdCJPPysqhWYw9jD97xtG5WqSJm', limit, 1.0, 1.0);
    });
  });

  it('Submit transaction', function(done) {
    const remote = new Remote();
    const transaction = new Transaction(remote).accountSet('r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe');

    assert.strictEqual(transaction.callback, undefined);
    assert.strictEqual(transaction._errorHandler, undefined);
    assert.strictEqual(transaction._successHandler, undefined);
    assert.strictEqual(transaction.listeners('error').length, 1);

    const account = remote.addAccount('r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe');

    account._transactionManager._nextSequence = 1;

    account._transactionManager._request = function(tx) {
      tx.emit('success', { });
    };

    transaction.complete = function() {
      return this;
    };

    let receivedSuccess = false;

    transaction.once('success', function() {
      receivedSuccess = true;
    });

    function submitCallback(err) {
      setImmediate(function() {
        assert.ifError(err);
        assert(receivedSuccess);
        done();
      });
    }

    transaction.submit(submitCallback);

    assert(transaction instanceof Transaction);
    assert.strictEqual(transaction.callback, submitCallback);
    assert.strictEqual(typeof transaction._errorHandler, 'function');
    assert.strictEqual(typeof transaction._successHandler, 'function');
  });

  it('Submit transaction - submission error', function(done) {
    const remote = new Remote();

    const transaction = new Transaction(remote).accountSet('r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe');

    const account = remote.addAccount('r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWe');

    account._transactionManager._nextSequence = 1;

    account._transactionManager._request = function(tx) {
      tx.emit('error', new Error('Test error'));
    };

    transaction.complete = function() {
      return this;
    };

    let receivedError = false;

    transaction.once('error', function() {
      receivedError = true;
    });

    function submitCallback(err) {
      setImmediate(function() {
        assert(err);
        assert.strictEqual(err.constructor.name, 'RippleError');
        assert(receivedError);
        done();
      });
    }

    transaction.submit(submitCallback);
  });

  it('Submit transaction - submission error, no remote', function(done) {
    const transaction = new Transaction();

    transaction.once('error', function(error) {
      assert(error);
      assert.strictEqual(error.message, 'No remote found');
      done();
    });

    transaction.submit();
  });

  it('Submit transaction - invalid account', function(done) {
    const remote = new Remote();
    assert.throws(function() {
      new Transaction(remote).accountSet('r36xtKNKR43SeXnGn7kN4r4JdQzcrkqpWeZ');
    });
    done();
  });

  it('Abort submission on presubmit', function(done) {
    const remote = new Remote();
    remote.setSecret('rJaT8TafQfYJqDm8aC5n3Yx5yWEL2Ery79', 'snPwFATthTkKnGjEW73q3TL4yci1Q');

    const server = new Server(remote, 'wss://s1.ripple.com:443');
    server._computeFee = function() {
      return '12';
    };
    server._connected = true;

    remote._servers.push(server);
    remote._connected = true;
    remote._ledger_current_index = 1;

    const transaction = new Transaction(remote).accountSet('rJaT8TafQfYJqDm8aC5n3Yx5yWEL2Ery79');
    const account = remote.account('rJaT8TafQfYJqDm8aC5n3Yx5yWEL2Ery79');

    account._transactionManager._nextSequence = 1;

    transaction.once('presubmit', function() {
      transaction.abort();
    });

    transaction.submit(function(err) {
      setImmediate(function() {
        assert(err);
        assert.strictEqual(err.result, 'tejAbort');
        done();
      });
    });
  });

  it('Get min ledger', function() {
    const queue = new TransactionQueue();

    // Randomized submit indexes
    const indexes = [
      28093,
      456944,
      347213,
      165662,
      729760,
      808990,
      927393,
      925550,
      872298,
      543305
    ];

    indexes.forEach(function(index) {
      const tx = new Transaction();
      tx.initialSubmitIndex = index;
      queue.push(tx);
    });

    // Pending queue sorted by submit index
    const sorted = queue._queue.slice().sort(function(a, b) {
      return a.initialSubmitIndex - b.initialSubmitIndex;
    });

    sorted.forEach(function(tx) {
      assert.strictEqual(queue.getMinLedger(), tx.initialSubmitIndex);
      queue.remove(tx);
    });
  });
});
