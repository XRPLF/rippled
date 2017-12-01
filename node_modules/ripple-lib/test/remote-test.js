/* eslint-disable no-new, max-len, no-comma-dangle, indent */

'use strict';

const assert = require('assert-diff');
const lodash = require('lodash');
const ripple = require('ripple-lib');
const Remote = require('ripple-lib').Remote;
const Server = require('ripple-lib').Server;
const Transaction = require('ripple-lib').Transaction;
const UInt160 = require('ripple-lib').UInt160;
const Currency = require('ripple-lib').Currency;
const Amount = require('ripple-lib').Amount;
const PathFind = require('ripple-lib')._test.PathFind;
const Log = require('ripple-lib')._test.Log;

let options, remote, callback;

const ADDRESS = 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS';
const LEDGER_INDEX = 9592219;
const LEDGER_HASH =
  'B4FD84A73DBD8F0DA9E320D137176EBFED969691DC0AAC7882B76B595A0841AE';
const PAGING_MARKER =
  '29F992CC252056BF690107D1E8F2D9FBAFF29FF107B62B1D1F4E4E11ADF2CC73';
const TRANSACTION_HASH =
  '14576FFD5D59FFA73CAA90547BE4DE09926AAB59E981306C32CCE04408CBF8EA';
const TX_JSON = {
  Flags: 0,
  TransactionType: 'Payment',
  Account: ADDRESS,
  Destination: ripple.UInt160.ACCOUNT_ONE,
  Amount: {
    value: '1',
    currency: 'USD',
    issuer: ADDRESS
  }
};

function makeServer(url) {
  const server = new Server(new process.EventEmitter(), url);
  server._connected = true;
  return server;
}

describe('Remote', function() {
  const initialLogEngine = Log.getEngine();

  beforeEach(function() {
    options = {
      trusted: true,
      servers: ['wss://s1.ripple.com:443']
    };
    remote = new Remote(options);
  });

  afterEach(function() {
    Log.setEngine(initialLogEngine);
  });

  it('Server initialization -- url object', function() {
    remote = new Remote({
      servers: [{host: 's-west.ripple.com', port: 443, secure: true}]
    });
    assert(Array.isArray(remote._servers));
    assert(remote._servers[0] instanceof Server);
    assert.strictEqual(remote._servers[0]._url, 'wss://s-west.ripple.com:443');
  });

  it('Server initialization -- url object -- no secure property', function() {
    remote = new Remote({
      servers: [{host: 's-west.ripple.com', port: 443}]
    });
    assert(Array.isArray(remote._servers));
    assert(remote._servers[0] instanceof Server);
    assert.strictEqual(remote._servers[0]._url, 'wss://s-west.ripple.com:443');
  });

  it('Server initialization -- url object -- secure: false', function() {
    remote = new Remote({
      servers: [{host: 's-west.ripple.com', port: 443, secure: false}]
    });
    assert(Array.isArray(remote._servers));
    assert(remote._servers[0] instanceof Server);
    assert.strictEqual(remote._servers[0]._url, 'ws://s-west.ripple.com:443');
  });

  it('Server initialization -- url object -- string port', function() {
    remote = new Remote({
      servers: [{host: 's-west.ripple.com', port: '443', secure: true}]
    });
    assert(Array.isArray(remote._servers));
    assert(remote._servers[0] instanceof Server);
    assert.strictEqual(remote._servers[0]._url, 'wss://s-west.ripple.com:443');
  });

  it('Server initialization -- url object -- invalid host', function() {
    assert.throws(
      function() {
        new Remote({
          servers: [{host: '+', port: 443, secure: true}]
        });
      }, Error);
  });

  it('Server initialization -- url object -- invalid port', function() {
    assert.throws(
      function() {
        new Remote({
          servers: [{host: 's-west.ripple.com', port: 'null', secure: true}]
        });
      }, TypeError);
  });

  it('Server initialization -- url object -- port out of range', function() {
    assert.throws(
      function() {
        new Remote({
          servers: [{host: 's-west.ripple.com', port: 65537, secure: true}]
        });
      }, Error);
  });

  it('Server initialization -- url string', function() {
    remote = new Remote({
      servers: ['wss://s-west.ripple.com:443']
    });
    assert(Array.isArray(remote._servers));
    assert(remote._servers[0] instanceof Server);
    assert.strictEqual(remote._servers[0]._url, 'wss://s-west.ripple.com:443');
  });

  it('Server initialization -- url string -- ws://', function() {
    remote = new Remote({
      servers: ['ws://s-west.ripple.com:443']
    });
    assert(Array.isArray(remote._servers));
    assert(remote._servers[0] instanceof Server);
    assert.strictEqual(remote._servers[0]._url, 'ws://s-west.ripple.com:443');
  });

  it('Server initialization -- url string -- invalid host', function() {
    assert.throws(
      function() {
        new Remote({
          servers: ['ws://+:443']
        });
      }, Error
    );
  });

  /*
  "url" module used in server parses such urls with error, it return
  null for port, so in this case default port will be used

  it('Server initialization -- url string -- invalid port', function() {
    assert.throws(
      function() {
        new Remote({
          servers: ['ws://s-west.ripple.com:invalid']
        });
      }, Error
    );
  });
  */

  it('Server initialization -- url string -- port out of range', function() {
    assert.throws(
      function() {
        new Remote({
          servers: ['ws://s-west.ripple.com:65537']
        });
      }, Error
    );
  });

  it('Server initialization -- set max_fee', function() {
    remote = new Remote({max_fee: 10});
    assert.strictEqual(remote.max_fee, 10);
    remote = new Remote({max_fee: 1234567890});
    assert.strictEqual(remote.max_fee, 1234567890);
  });

  it('Server initialization -- set max_fee -- invalid', function() {
    assert.throws(function() {
      new Remote({max_fee: '1234567890'});
    });
  });

  it('Server initialization -- set trusted', function() {
    remote = new Remote({trusted: true});
    assert.strictEqual(remote.trusted, true);
  });
  it('Server initialization -- set trusted -- invalid', function() {
    assert.throws(function() {
      new Remote({trusted: '1234567890'});
    });
  });

  it('Server initialization -- set trace', function() {
    remote = new Remote({trace: true});
    assert.strictEqual(remote.trace, true);
  });
  it('Server initialization -- set trace -- invalid', function() {
    assert.throws(function() {
      new Remote({trace: '1234567890'});
    });
  });

  it('Server initialization -- set allow_partial_history', function() {
    remote = new Remote({allow_partial_history: true});
    assert.strictEqual(remote.allow_partial_history, true);
  });
  it('Server initialization -- set allow_partial_history -- invalid',
     function() {
    assert.throws(function() {
      new Remote({allow_partial_history: '1234567890'});
    });
  });

  it('Server initialization -- set max_attempts', function() {
    remote = new Remote({max_attempts: 10});
    assert.strictEqual(remote.max_attempts, 10);
  });
  it('Server initialization -- set max_attempts -- invalid', function() {
    assert.throws(function() {
      new Remote({max_attempts: '1234567890'});
    });
  });

  it('Server initialization -- set fee_cushion', function() {
    remote = new Remote({fee_cushion: 1.3});
    assert.strictEqual(remote.fee_cushion, 1.3);
  });
  it('Server initialization -- set fee_cushion -- invalid', function() {
    assert.throws(function() {
      new Remote({fee_cushion: '1234567890'});
    });
  });

  it('Server initialization -- set local_signing', function() {
    remote = new Remote({local_signing: false});
    assert.strictEqual(remote.local_signing, false);
  });
  it('Server initialization -- set local_signing -- invalid', function() {
    assert.throws(function() {
      remote = new Remote({local_signing: '1234567890'});
    });
  });
  it('Server initialization -- set local_fee', function() {
    remote = new Remote({local_fee: false});
    assert.strictEqual(remote.local_fee, true);
    remote = new Remote({local_signing: false, local_fee: false});
    assert.strictEqual(remote.local_fee, false);
  });
  it('Server initialization -- set local_fee -- invalid', function() {
    assert.throws(function() {
      new Remote({
        local_signing: false,
        local_fee: '1234567890'
      });
    });
  });
  it('Server initialization -- set local_sequence', function() {
    remote = new Remote({local_sequence: false});
    assert.strictEqual(remote.local_sequence, true);
    remote = new Remote({local_signing: false, local_sequence: false});
    assert.strictEqual(remote.local_sequence, false);
  });
  it('Server initialization -- set local_sequence -- invalid', function() {
    assert.throws(function() {
      new Remote({
        local_signing: false,
        local_sequence: '1234567890'
      });
    });
  });

  it('Server initialization -- set canonical_signing', function() {
    assert.strictEqual(new Remote({canonical_signing: false})
                       .canonical_signing, false);
  });
  it('Server initialization -- set canonical_signing -- invalid', function() {
    assert.throws(function() {
      new Remote({canonical_signing: '1234567890'});
    });
  });

  it('Server initialization -- set submission_timeout', function() {
    assert.strictEqual(new Remote({submission_timeout: 10})
                       .submission_timeout, 10);
  });
  it('Server initialization -- set submission_timeout -- invalid', function() {
    assert.throws(function() {
      new Remote({submission_timeout: '1234567890'});
    });
  });

  it('Server initialization -- set last_ledger_offset', function() {
    assert.strictEqual(new Remote({last_ledger_offset: 10})
                       .last_ledger_offset, 10);
  });
  it('Server initialization -- set last_ledger_offset -- invalid', function() {
    assert.throws(function() {
      new Remote({last_ledger_offset: '1234567890'});
    });
  });

  it('Server initialization -- set servers', function() {
    assert.deepEqual(new Remote({servers: []}).servers, [ ]);
  });
  it('Server initialization -- set servers -- invalid', function() {
    assert.throws(function() {
      new Remote({servers: '1234567890'});
    });
  });

  it('Automatic transactions subscription', function(done) {
    let i = 0;

    remote.request = function(request) {
      switch (++i) {
        case 1:
          assert.strictEqual(request.message.command, 'subscribe');
          break;
        case 2:
          assert.strictEqual(request.message.command, 'unsubscribe');
          done();
          break;
      }
      assert.deepEqual(request.message.streams, ['transactions']);
    };

    remote.on('transaction', function() {});
    remote.removeAllListeners('transaction');
  });

  it('Check is valid message', function() {
    assert(Remote.isValidMessage({type: 'response'}));
    assert(!Remote.isValidMessage({}));
    assert(!Remote.isValidMessage(''));
  });
  it('Check is valid ledger data', function() {
    assert(Remote.isValidLedgerData({
      fee_base: 10,
      fee_ref: 10,
      ledger_hash: LEDGER_HASH,
      ledger_index: 1,
      ledger_time: 1,
      reserve_base: 10,
      reserve_inc: 10
    }));
    assert(!Remote.isValidLedgerData({
        fee_base: 10,
        fee_ref: 10,
        ledger_hash: LEDGER_HASH,
        ledger_index: 1,
        ledger_time: 1,
        reserve_base: 10,
        reserve_inc: '10'
      }));
    assert(!Remote.isValidLedgerData({
        fee_base: 10,
        fee_ref: 10,
        ledger_hash: LEDGER_HASH,
        ledger_index: 1,
        reserve_base: 10,
        reserve_inc: 10
      }));
  });
  it('Check is valid load status', function() {
    assert(Remote.isValidLoadStatus({
      load_base: 10,
      load_factor: 10
    }));
    assert(!Remote.isValidLoadStatus({
        load_base: 10,
        load_factor: '10'
      }));
    assert(!Remote.isValidLoadStatus({
        load_base: 10
      }));
  });
  it('Check is validated', function() {
    assert(Remote.isValidated({validated: true}));
    assert(!Remote.isValidated({validated: false}));
    assert(!Remote.isValidated({validated: 'true'}));
    assert(!Remote.isValidated({}));
    assert(!Remote.isValidated(null));
  });

  it('Set state', function() {
    let i = 0;
    remote.on('state', function(state) {
      switch (++i) {
        case 1:
          assert.strictEqual(state, 'online');
          break;
        case 2:
          assert.strictEqual(state, 'offline');
          break;
      }
      assert.strictEqual(state, remote.state);
    });
    remote._setState('online');
    remote._setState('online');
    remote._setState('offline');
    remote._setState('offline');
    assert.strictEqual(i, 2);
  });

  it('Set trace', function() {
    remote.setTrace(true);
    assert.strictEqual(remote.trace, true);
    remote.setTrace();
    assert.strictEqual(remote.trace, true);
    remote.setTrace(false);
    assert.strictEqual(remote.trace, false);
  });

  it('Set server fatal', function() {
    remote.setServerFatal();
    assert.strictEqual(remote._server_fatal, true);
  });

  it('Add server', function() {
    const server = remote.addServer('wss://s1.ripple.com:443');
    assert(server instanceof Server);

    let i = 0;
    remote.once('connect', function() {
      assert.strictEqual(remote._connection_count, 1);
      ++i;
    });
    remote.once('disconnect', function() {
      assert.strictEqual(remote._connection_count, 0);
      ++i;
    });

    server.emit('connect');
    server.emit('disconnect');

    assert.strictEqual(i, 2, 'Remote did not receive all server events');
  });
  it('Add server -- primary server', function() {
    const server = remote.addServer({
      host: 's1.ripple.com',
      port: 443,
      secure: true,
      primary: true
    });

    assert(server instanceof Server);
    assert.strictEqual(remote._servers.length, 2);
    assert.strictEqual(remote._servers[1], server);

    let i = 0;
    remote.once('connect', function() {
      assert.strictEqual(remote._connection_count, 1);
      assert.strictEqual(remote._primary_server, server);
      remote.setPrimaryServer(remote._servers[0]);
      assert.strictEqual(server._primary, false);
      assert.strictEqual(remote._primary_server, remote._servers[0]);
      ++i;
    });

    server.emit('connect');

    assert.strictEqual(i, 1, 'Remote did not receive all server events');
  });

  it('Connect', function() {
    remote.addServer('wss://s1.ripple.com:443');

    let i = 0;
    remote._servers.forEach(function(s) {
      s.connect = function() {
        ++i;
      };
    });

    remote.connect();

    assert.strictEqual(remote._should_connect, true);
    assert.strictEqual(i, 2, 'Did not attempt connect to all servers');
  });

  it('Connect -- with callback', function(done) {
    remote.addServer('wss://s1.ripple.com:443');

    let i = 0;
    remote._servers.forEach(function(s) {
      s.connect = function() {
        ++i;
      };
    });

    remote.connect(done);

    assert.strictEqual(remote._should_connect, true);
    assert.strictEqual(i, 2, 'Did not attempt connect to all servers');

    remote._servers[0].emit('connect');
  });

  it('Connect -- no servers', function() {
    remote._servers = [];
    assert.throws(function() {
      remote.connect();
    });
  });

  it('Disconnect', function() {
    remote.addServer('wss://s1.ripple.com:443');

    let i = 0;
    remote._servers.forEach(function(s) {
      s.disconnect = function() {
        ++i;
      };
      s.emit('connect');
    });

    remote.disconnect();

    assert.strictEqual(remote._should_connect, false);
    assert.strictEqual(i, 2, 'Did not attempt disconnect to all servers');
  });
  it('Disconnect -- with callback', function(done) {
    remote.addServer('wss://s1.ripple.com:443');

    let i = 0;
    remote._servers.forEach(function(s) {
      s.disconnect = function() {
        ++i;
      };
      s.emit('connect');
    });

    remote.disconnect(done);

    assert.strictEqual(remote._should_connect, false);
    assert.strictEqual(i, 2, 'Did not attempt disconnect to all servers');

    remote._servers.forEach(function(s) {
      s.emit('disconnect');
    });
  });
  it('Disconnect -- unconnected', function(done) {
    remote.addServer('wss://s1.ripple.com:443');

    let i = 0;
    remote._servers.forEach(function(s) {
      s.disconnect = function() {
        ++i;
      };
    });

    remote.disconnect(done);

    assert.strictEqual(i, 0, 'Should not attempt disconnect');
  });
  it('Disconnect -- no servers', function() {
    remote._servers = [];
    assert.throws(function() {
      remote.disconnect();
    });
  });

  it('Handle server message -- ledger', function() {
    const message = {
      type: 'ledgerClosed',
      fee_base: 10,
      fee_ref: 10,
      ledger_hash:
        'F824560DD788E5E4B65F5843A6616872873EAB74AA759C73A992355FFDFC4237',
      ledger_index: 11368614,
      ledger_time: 475696280,
      reserve_base: 20000000,
      reserve_inc: 5000000,
      txn_count: 9,
      validated_ledgers: '32570-11368614'
    };

    remote.once('ledger_closed', function(l) {
      assert.deepEqual(l, message);
      assert.strictEqual(remote.getLedgerHash(), message.ledger_hash);
    });
    remote._servers[0].emit('connect');
    remote._servers[0].emit('message', message);
  });
  it('Handle server message -- ledger', function(done) {
    const message = {
      type: 'ledgerClosed',
      fee_base: 10,
      fee_ref: 10,
      ledger_hash:
        'F824560DD788E5E4B65F5843A6616872873EAB74AA759C73A992355FFDFC4237',
      ledger_index: 11368614,
      ledger_time: 475696280,
      reserve_base: 20000000,
      reserve_inc: 5000000,
      txn_count: 9,
      validated_ledgers: '32570-11368614'
    };

    remote.once('ledger_closed', function(l) {
      assert.deepEqual(l, message);
      done();
    });
    remote._servers[0].emit('message', message);

    setImmediate(function() {
      remote._servers[0].emit('connect');
    });
  });
  it('Handle server message -- server status', function() {
    const message = {
      type: 'serverStatus',
      load_base: 256,
      load_factor: 256,
      server_status: 'full'
    };

    remote.once('server_status', function(l) {
      assert.deepEqual(l, message);
    });
    remote._servers[0].emit('message', message);
    remote._servers[0].emit('connect');
  });
  it('Handle server message -- validation received', function() {
    const message = {
      type: 'validationReceived',
      ledger_hash:
        '96D9E225F10C22D5047B87597939F94024F4180609227D1EB7E9D1CE9A428620',
      validation_public_key:
        'n9L81uNCaPgtUJfaHh89gmdvXKAmSt5Gdsw2g1iPWaPkAHW5Nm4C',
      signature:
        '304402207E221CF0679B1A52BC07C4B97C56B93392F8BB53DFB52B821828118A740' +
        '9F3E302202669AD632D9CD288B20A0A98DBC50DD3961EC50B95B138A9DCBDC11506' +
        'F63646'
    };

    remote.once('validation_received', function(l) {
      assert.deepEqual(l, message);
    });
    remote._servers[0].emit('message', message);
    remote._servers[0].emit('connect');
  });
  it('Handle server message -- transaction', function() {
    const message = require('./fixtures/transaction');

    remote.once('transaction', function(l) {
      assert.deepEqual(l, message);
    });
    remote._servers[0].emit('connect');
    remote._servers[0].emit('message', message);
  });
  it('Handle server message -- transaction -- duplicate hashes', function() {
    const message = require('./fixtures/transaction');
    let i = 0;

    remote.once('transaction', function(l) {
      assert.deepEqual(l, message);
      ++i;
    });

    remote._servers[0].emit('connect');
    remote._servers[0].emit('message', message);
    remote._servers[0].emit('message', message);
    remote._servers[0].emit('message', message);
    assert.strictEqual(i, 1);
  });
  it('Handle server message -- '
     + 'transaction -- with account notification', function() {
    const message = require('./fixtures/transaction');
    let i = 0;
    const account = remote.addAccount(message.transaction.Account);

    account.once('transaction', function(t) {
      assert.deepEqual(t, message);
      ++i;
    });

    remote.once('transaction', function(l) {
      assert.deepEqual(l, message);
      ++i;
    });

    remote._servers[0].emit('connect');
    remote._servers[0].emit('message', message);
    assert.strictEqual(i, 2);
  });
  it('Handle server message -- '
     + 'transaction proposed -- with account notification', function() {
    const message = require('./fixtures/transaction-proposed');
    let i = 0;
    const account = remote.addAccount(message.transaction.Account);

    account.once('transaction', function(t) {
      assert.deepEqual(t, message);
      ++i;
    });

    remote.once('transaction', function(l) {
      assert.deepEqual(l, message);
      ++i;
    });

    remote._servers[0].emit('connect');
    remote._servers[0].emit('message', message);
    assert.strictEqual(i, 2);
  });
  it('Handle server message -- transaction -- with orderbook notification',
     function() {
    const message = require('./fixtures/transaction-offercreate');
    let i = 0;
    const orderbook = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: 'rJy64aCJLP3vf8o3WPKn4iQKtfpjh6voAR',
      currency_pays: 'XRP'
    });

    orderbook._subscribed = true;
    orderbook._synced = true;

    orderbook.once('transaction', function(t) {
      assert.deepEqual(t.transaction, message.transaction);
      assert.deepEqual(t.meta, message.meta);
      ++i;
    });

    remote.once('transaction', function(l) {
      assert.deepEqual(l, message);
      ++i;
    });

    remote._servers[0].emit('connect');
    remote._servers[0].emit('message', message);
    assert.strictEqual(i, 2);
  });
  it('Handle server message -- path find', function() {
    const message = require('./fixtures/pathfind');
    let i = 0;

    const amount = Amount.from_json({
      currency: 'USD',
      issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
      value: '0.001'
    });
    const path = new PathFind(remote,
      'r9cZA1mLK5R5Am25ArfXFmqgNwjZgnfk59',
      'r9cZA1mLK5R5Am25ArfXFmqgNwjZgnfk59',
      amount
    );

    path.once('update', function(p) {
      assert.deepEqual(p, message);
      ++i;
    });
    remote.once('path_find_all', function(p) {
      assert.deepEqual(p, message);
      ++i;
    });

    remote._cur_path_find = path;
    remote._servers[0].emit('connect');
    remote._servers[0].emit('message', message);

    assert.strictEqual(i, 2);
  });
  it('Handle server message -- invalid message', function() {
    // Silence error log
    Log.setEngine(Log.engines.none);

    require('./fixtures/pathfind');
    let i = 0;

    remote.on('error', function(e) {
      assert(/^Unexpected response from remote/.test(e.message));
      ++i;
    });
    remote._servers[0].emit('message', '1');
    remote._servers[0].emit('message', {});
    remote._servers[0].emit('message', {type: 'response'});
    remote._servers[0].emit('message', JSON.stringify({type: 'response'}));

    assert.strictEqual(i, 3, 'Failed to receive all invalid message errors');
  });

  it('Get server', function() {
    remote.addServer('wss://sasdf.ripple.com:443');

    remote.connect();
    remote._connected = true;
    remote._servers.forEach(function(s) {
      s._connected = true;
    });

    const message = {
      type: 'ledgerClosed',
      fee_base: 10,
      fee_ref: 10,
      ledger_hash:
        'F824560DD788E5E4B65F5843A6616872873EAB74AA759C73A992355FFDFC4237',
      ledger_index: 1,
      ledger_time: 475696280,
      reserve_base: 20000000,
      reserve_inc: 5000000,
      txn_count: 9,
      validated_ledgers: '32570-11368614'
    };

    remote._servers[0].emit('message', message);
    assert.strictEqual(remote.getServer(), remote._servers[0]);

    message.ledger_index += 1;

    remote._servers[1].emit('message', message);
    assert.strictEqual(remote.getServer(), remote._servers[1]);
  });
  it('Get server -- no servers', function() {
    assert.strictEqual(new Remote().getServer(), null);
  });
  it('Get server -- no connected servers', function() {
    remote.addServer('wss://sasdf.ripple.com:443');
    assert.strictEqual(remote._servers.length, 2);
    assert.strictEqual(remote.getServer(), null);
  });
  it('Get server -- primary server', function() {
    const server = remote.addServer({
      host: 'sasdf.ripple.com',
      port: 443,
      secure: true,
      primary: true
    });

    remote.connect();
    server._connected = true;

    assert.strictEqual(remote.getServer().getServerID(), server.getServerID());
  });

  it('Parse binary transaction', function() {
    const binaryTransaction = require('./fixtures/binary-transaction.json');

    const parsedSourceTag = Remote.parseBinaryTransaction(
      binaryTransaction.PaymentWithSourceTag.binary);
    assert.deepEqual(parsedSourceTag,
                     binaryTransaction.PaymentWithSourceTag.parsed);

    const parsedMemosAndPaths = Remote.parseBinaryTransaction(
      binaryTransaction.PaymentWithMemosAndPaths.binary);
    assert.deepEqual(parsedMemosAndPaths,
                     binaryTransaction.PaymentWithMemosAndPaths.parsed);

    const parsedPartialPayment = Remote.parseBinaryTransaction(
      binaryTransaction.PartialPayment.binary);
    assert.deepEqual(parsedPartialPayment,
                     binaryTransaction.PartialPayment.parsed);

    const parsedOfferCreate = Remote.parseBinaryTransaction(
      binaryTransaction.OfferCreate.binary);
    assert.deepEqual(parsedOfferCreate, binaryTransaction.OfferCreate.parsed);

    const parsedPartialPaymentWithXRPDelieveredAmount =
      Remote.parseBinaryTransaction(
        binaryTransaction.PartialPaymentWithXRPDeliveredAmount.binary);

    assert.deepEqual(parsedPartialPaymentWithXRPDelieveredAmount,
                     binaryTransaction
                     .PartialPaymentWithXRPDeliveredAmount
                     .parsed);
  });

  it('Parse binary account transaction', function() {
    const binaryAccountTransaction =
      require('./fixtures/binary-account-transaction.json');

    const parsed = Remote.parseBinaryAccountTransaction(
      binaryAccountTransaction.OfferCreate.binary);
    assert.deepEqual(parsed, binaryAccountTransaction.OfferCreate.parsed);

    const parsedPartialPayment = Remote.parseBinaryAccountTransaction(
      binaryAccountTransaction.PartialPayment.binary);
    assert.deepEqual(parsedPartialPayment,
                     binaryAccountTransaction.PartialPayment.parsed);

    const parsedPayment = Remote.parseBinaryAccountTransaction(
      binaryAccountTransaction.Payment.binary);
    assert.deepEqual(parsedPayment, binaryAccountTransaction.Payment.parsed);
  });

  it('Parse binary ledger', function() {
    const binaryLedgerData = require('./fixtures/binary-ledger-data.json');

    const parsedAccountRoot =
      Remote.parseBinaryLedgerData(binaryLedgerData.AccountRoot.binary);
    assert.deepEqual(parsedAccountRoot, binaryLedgerData.AccountRoot.parsed);

    const parsedOffer =
      Remote.parseBinaryLedgerData(binaryLedgerData.Offer.binary);
    assert.deepEqual(parsedOffer, binaryLedgerData.Offer.parsed);

    const parsedDirectoryNode =
      Remote.parseBinaryLedgerData(binaryLedgerData.DirectoryNode.binary);
    assert.deepEqual(parsedDirectoryNode,
                     binaryLedgerData.DirectoryNode.parsed);

    const parsedRippleState =
      Remote.parseBinaryLedgerData(binaryLedgerData.RippleState.binary);
    assert.deepEqual(parsedRippleState, binaryLedgerData.RippleState.parsed);
  });

  it('Prepare currency', function() {
    assert.deepEqual(Remote.prepareCurrencies({
      issuer: 'rGr9PjmVe7MqEXTSbd3njhgJc2s5vpHV54',
      currency: 'USD',
      value: 1
    }), {
      issuer: 'rGr9PjmVe7MqEXTSbd3njhgJc2s5vpHV54',
      currency: '0000000000000000000000005553440000000000'
    });
  });

  it('Get transaction fee', function() {
    remote._connected = true;
    remote._servers[0]._connected = true;
    assert.strictEqual(remote.feeTx(10).to_json(), '12');
    remote._servers = [];
    assert.throws(function() {
      remote.feeTx(10).to_json();
    });
  });
  it('Get transaction fee units', function() {
    remote._connected = true;
    remote._servers[0]._connected = true;
    assert.strictEqual(remote.feeTxUnit(), 1.2);
    remote._servers = [];
    assert.throws(function() {
      remote.feeTxUnit(10).to_json();
    });
  });
  it('reserve() before reserve rate known', function() {
    remote._connected = true;
    remote._servers[0]._connected = true;
    // Throws because the server has not had reserve_inc, reserve_base set
    assert.throws(function() {
      remote.reserve(10).to_json();
    });
  });

  it('Initiate request', function() {
    const request = remote.requestServerInfo();

    assert.deepEqual(request.message, {
      command: 'server_info',
      id: undefined
    });

    let i = 0;
    remote._connected = true;
    remote._servers[0]._connected = true;
    remote._servers[0]._request = function() {
      ++i;
    };
    remote.request(request);

    assert.strictEqual(i, 1, 'Did not initiate request');
  });
  it('Initiate request -- with request name', function() {
    const request = remote.request('server_info');

    assert.deepEqual(request.message, {
      command: 'server_info',
      id: undefined
    });

    let i = 0;
    remote._connected = true;
    remote._servers[0]._connected = true;
    remote._servers[0]._request = function() {
      ++i;
    };
    remote.request(request);

    assert.strictEqual(i, 1, 'Did not initiate request');
  });
  it('Initiate request -- with invalid request name', function() {
    assert.throws(function() {
      remote.request('server_infoz');
    });
  });
  it('Initiate request -- with invalid request', function() {
    assert.throws(function() {
      remote.request({});
    });
    assert.throws(function() {
      remote.request({command: 'server_info', id: 1});
    });
  });
  it('Initiate request -- set non-existent servers', function() {
    const request = remote.requestServerInfo();
    request.setServer('wss://s-east.ripple.com:443');
    assert.strictEqual(request.server, null);
    assert.throws(function() {
      remote._connected = true;
      remote.request(request);
    });
  });

  it('Construct ledger request', function() {
    const request = remote.requestLedger();
    assert.deepEqual(request.message, {
      command: 'ledger',
      id: undefined
    });
  });
  it('Construct ledger request -- with ledger index', function() {
    let request = remote.requestLedger({ledger: 1});
    assert.deepEqual(request.message, {
      command: 'ledger',
      id: undefined,
      ledger_index: 1
    });
    request = remote.requestLedger({ledger_index: 1});
    assert.deepEqual(request.message, {
      command: 'ledger',
      id: undefined,
      ledger_index: 1
    });
    request = remote.requestLedger(1);
    assert.deepEqual(request.message, {
      command: 'ledger',
      id: undefined,
      ledger_index: 1
    });
    request = remote.requestLedger(null);
    assert.deepEqual(request.message, {
      command: 'ledger',
      id: undefined
    });
  });
  it('Construct ledger request -- with ledger hash', function() {
    let request = remote.requestLedger({ledger: LEDGER_HASH});
    assert.deepEqual(request.message, {
      command: 'ledger',
      id: undefined,
      ledger_hash: LEDGER_HASH
    });

    request = remote.requestLedger({ledger_hash: LEDGER_HASH});

    assert.deepEqual(request.message, {
      command: 'ledger',
      id: undefined,
      ledger_hash: LEDGER_HASH
    });

    request = remote.requestLedger(LEDGER_HASH);

    assert.deepEqual(request.message, {
      command: 'ledger',
      id: undefined,
      ledger_hash: LEDGER_HASH
    });
  });
  it('Construct ledger request -- with ledger identifier', function() {
    let request = remote.requestLedger({ledger: 'validated'});

    assert.deepEqual(request.message, {
      command: 'ledger',
      id: undefined,
      ledger_index: 'validated'
    });

    request = remote.requestLedger({ledger: 'current'});

    assert.deepEqual(request.message, {
      command: 'ledger',
      id: undefined,
      ledger_index: 'current'
    });

    request = remote.requestLedger('validated');

    assert.deepEqual(request.message, {
      command: 'ledger',
      id: undefined,
      ledger_index: 'validated'
    });

    request = remote.requestLedger({validated: true});

    assert.deepEqual(request.message, {
      command: 'ledger',
      id: undefined,
      ledger_index: 'validated'
    });
  });
  it('Construct ledger request -- with transactions', function() {
    const request = remote.requestLedger({
      ledger: 'validated',
      transactions: true
    });
    assert.deepEqual(request.message, {
      command: 'ledger',
      id: undefined,
      ledger_index: 'validated',
      transactions: true
    });
  });

  it('Construct ledger_closed request', function() {
    const request = remote.requestLedgerClosed();
    assert.deepEqual(request.message, {
      command: 'ledger_closed',
      id: undefined
    });
  });
  it('Construct ledger_header request', function() {
    const request = remote.requestLedgerHeader();
    assert.deepEqual(request.message, {
      command: 'ledger_header',
      id: undefined
    });
  });
  it('Construct ledger_current request', function() {
    const request = remote.requestLedgerCurrent();
    assert.deepEqual(request.message, {
      command: 'ledger_current',
      id: undefined
    });
  });

  it('Construct ledger_data request -- with ledger hash', function() {
    const request = remote.requestLedgerData({
      ledger: LEDGER_HASH,
      limit: 5
    });

    assert.deepEqual(request.message, {
      command: 'ledger_data',
      id: undefined,
      binary: true,
      ledger_hash: LEDGER_HASH,
      limit: 5
    });
  });

  it('Construct ledger_data request -- with ledger index', function() {
    const request = remote.requestLedgerData({
      ledger: LEDGER_INDEX,
      limit: 5
    });

    assert.deepEqual(request.message, {
      command: 'ledger_data',
      id: undefined,
      binary: true,
      ledger_index: LEDGER_INDEX,
      limit: 5
    });
  });

  it('Construct ledger_data request -- no binary', function() {
    const request = remote.requestLedgerData({
      ledger: LEDGER_HASH,
      limit: 5,
      binary: false
    });

    assert.deepEqual(request.message, {
      command: 'ledger_data',
      id: undefined,
      binary: false,
      ledger_hash: LEDGER_HASH,
      limit: 5
    });
  });

  it('Construct server_info request', function() {
    const request = remote.requestServerInfo();
    assert.deepEqual(request.message, {
      command: 'server_info',
      id: undefined
    });
  });

  it('Construct peers request', function() {
    const request = remote.requestPeers();
    assert.deepEqual(request.message, {
      command: 'peers',
      id: undefined
    });
  });

  it('Construct connection request', function() {
    const request = remote.requestConnect('0.0.0.0', '443');
    assert.deepEqual(request.message, {
      command: 'connect',
      id: undefined,
      ip: '0.0.0.0',
      port: '443'
    });
  });

  it('Construct unl_add request', function() {
    const request = remote.requestUnlAdd('0.0.0.0');
    assert.deepEqual(request.message, {
      command: 'unl_add',
      node: '0.0.0.0',
      id: undefined
    });
  });

  it('Construct unl_list request', function() {
    const request = remote.requestUnlList();
    assert.deepEqual(request.message, {
      command: 'unl_list',
      id: undefined
    });
  });

  it('Construct unl_delete request', function() {
    const request = remote.requestUnlDelete('0.0.0.0');
    assert.deepEqual(request.message, {
      command: 'unl_delete',
      node: '0.0.0.0',
      id: undefined
    });
  });

  it('Construct subscribe request', function() {
    const request = remote.requestSubscribe(['server', 'ledger']);
    assert.deepEqual(request.message, {
      command: 'subscribe',
      id: undefined,
      streams: ['server', 'ledger']
    });
  });
  it('Construct unsubscribe request', function() {
    const request = remote.requestUnsubscribe(['server', 'ledger']);
    assert.deepEqual(request.message, {
      command: 'unsubscribe',
      id: undefined,
      streams: ['server', 'ledger']
    });
  });

  it('Construct ping request', function() {
    const request = remote.requestPing();
    assert.deepEqual(request.message, {
      command: 'ping',
      id: undefined
    });
  });
  it('Construct ping request -- with server', function() {
    const request = remote.requestPing('wss://s1.ripple.com:443');
    assert.strictEqual(request.server, remote._servers[0]);
    assert.deepEqual(request.message, {
      command: 'ping',
      id: undefined
    });
  });
  it('Construct account_currencies request', function() {
    let request = remote.requestAccountCurrencies({
      account: ADDRESS
    }, lodash.noop);

    assert.strictEqual(request.message.command, 'account_currencies');
    assert.strictEqual(request.message.account, ADDRESS);
    assert.strictEqual(request.requested, true);

    Log.setEngine(Log.engines.none);
    request = remote.requestAccountCurrencies(ADDRESS, lodash.noop);
    assert.strictEqual(request.message.command, 'account_currencies');
    assert.strictEqual(request.message.account, ADDRESS);
    assert.strictEqual(request.requested, true);
  });

  it('Construct account_info request', function() {
    let request = remote.requestAccountInfo({
      account: ADDRESS
    }, lodash.noop);

    assert.strictEqual(request.message.command, 'account_info');
    assert.strictEqual(request.message.account, ADDRESS);
    assert.strictEqual(request.requested, true);

    Log.setEngine(Log.engines.none);
    request = remote.requestAccountInfo(ADDRESS, lodash.noop);
    assert.strictEqual(request.message.command, 'account_info');
    assert.strictEqual(request.message.account, ADDRESS);
    assert.strictEqual(request.requested, true);
  });

  it('Construct account_info request -- with ledger index', function() {
    let request = remote.requestAccountInfo({
      account: ADDRESS,
      ledger: 9592219
    }, lodash.noop);
    assert.strictEqual(request.message.command, 'account_info');
    assert.strictEqual(request.message.account, ADDRESS);
    assert.strictEqual(request.message.ledger_index, 9592219);
    assert.strictEqual(request.requested, true);

    Log.setEngine(Log.engines.none);
    request = remote.requestAccountInfo(ADDRESS, 9592219, lodash.noop);
    assert.strictEqual(request.requested, true);

    assert.strictEqual(request.message.command, 'account_info');
    assert.strictEqual(request.message.account, ADDRESS);
    assert.strictEqual(request.message.ledger_index, 9592219);
  });

  it('Construct account_info request -- with ledger hash', function() {
     const request = remote.requestAccountInfo({
       account: ADDRESS,
       ledger: LEDGER_HASH
     }, lodash.noop);
     assert.strictEqual(request.message.command, 'account_info');
     assert.strictEqual(request.message.account, ADDRESS);
     assert.strictEqual(request.message.ledger_hash, LEDGER_HASH);
    assert.strictEqual(request.requested, true);
   });
   it('Construct account_info request -- with ledger identifier', function() {
     const request = remote.requestAccountInfo({
       account: ADDRESS,
       ledger: 'validated'
     }, lodash.noop);
     assert.strictEqual(request.message.command, 'account_info');
     assert.strictEqual(request.message.account, ADDRESS);
     assert.strictEqual(request.message.ledger_index, 'validated');
    assert.strictEqual(request.requested, true);
   });

   it('Construct account balance request -- with ledger index', function() {
     const request = remote.requestAccountBalance({
       account: ADDRESS,
       ledger: 9592219
     }, lodash.noop);
     assert.strictEqual(request.message.command, 'ledger_entry');
     assert.strictEqual(request.message.account_root, ADDRESS);
     assert.strictEqual(request.message.ledger_index, 9592219);
    assert.strictEqual(request.requested, true);
   });
   it('Construct account balance request -- with ledger hash', function() {
     const request = remote.requestAccountBalance({
       account: ADDRESS,
       ledger: LEDGER_HASH
     }, lodash.noop);
     assert.strictEqual(request.message.command, 'ledger_entry');
     assert.strictEqual(request.message.account_root, ADDRESS);
     assert.strictEqual(request.message.ledger_hash, LEDGER_HASH);
    assert.strictEqual(request.requested, true);
   });
   it('Construct account balance request -- with ledger identifier', function() {
     const request = remote.requestAccountBalance({
       account: ADDRESS,
       ledger: 'validated'
     }, lodash.noop);
     assert.strictEqual(request.message.command, 'ledger_entry');
     assert.strictEqual(request.message.account_root, ADDRESS);
     assert.strictEqual(request.message.ledger_index, 'validated');
    assert.strictEqual(request.requested, true);
   });

   it('Construct account flags request', function() {
     const request = remote.requestAccountFlags({account: ADDRESS}, lodash.noop);
     assert.strictEqual(request.message.command, 'ledger_entry');
     assert.strictEqual(request.message.account_root, ADDRESS);
    assert.strictEqual(request.requested, true);
   });
   it('Construct account owner count request', function() {
     let request = remote.requestOwnerCount({account: ADDRESS}, lodash.noop);
     assert.strictEqual(request.message.command, 'ledger_entry');
     assert.strictEqual(request.message.account_root, ADDRESS);
    assert.strictEqual(request.requested, true);

     Log.setEngine(Log.engines.none);
     request = remote.requestOwnerCount(ADDRESS, lodash.noop);

     assert.strictEqual(request.message.command, 'ledger_entry');
     assert.strictEqual(request.message.account_root, ADDRESS);
     assert.strictEqual(request.requested, true);
   });

   it('Construct account_lines request', function() {
     const request = remote.requestAccountLines({account: ADDRESS}, lodash.noop);
     assert.deepEqual(request.message, {
       command: 'account_lines',
       id: undefined,
       account: ADDRESS
     });
     assert.strictEqual(request.requested, true);
   });
   it('Construct account_lines request -- with peer', function() {
     const request = remote.requestAccountLines({
       account: ADDRESS,
       peer: ADDRESS
     }, lodash.noop);
     assert.deepEqual(request.message, {
       command: 'account_lines',
       id: undefined,
       account: ADDRESS,
       peer: ADDRESS
     });
    assert.strictEqual(request.requested, true);
   });
   it('Construct account_lines request -- with limit', function() {
     const request = remote.requestAccountLines({
       account: ADDRESS,
       limit: 100
     }, lodash.noop);
     assert.deepEqual(request.message, {
       command: 'account_lines',
       id: undefined,
       account: ADDRESS,
       limit: 100
     });
    assert.strictEqual(request.requested, true);
   });
   it('Construct account_lines request -- with limit and marker', function() {
     let request = remote.requestAccountLines({
       account: ADDRESS,
       limit: 100,
       marker: PAGING_MARKER,
       ledger: 9592219
     }, lodash.noop);
     assert.deepEqual(request.message, {
       command: 'account_lines',
       id: undefined,
       account: ADDRESS,
       limit: 100,
       marker: PAGING_MARKER,
       ledger_index: 9592219
     });
    assert.strictEqual(request.requested, true);

     Log.setEngine(Log.engines.none);
     request = remote.requestAccountLines(
       ADDRESS,
       null,
       9592219,
       100,
       PAGING_MARKER,
       lodash.noop
     );

     assert.deepEqual(request.message, {
       command: 'account_lines',
       id: undefined,
       account: ADDRESS,
       limit: 100,
       marker: PAGING_MARKER,
       ledger_index: 9592219
     });
    assert.strictEqual(request.requested, true);
   });
   it('Construct account_lines request -- with min limit', function() {
     assert.strictEqual(remote.requestAccountLines({
       account: ADDRESS, limit: 0
     }).message.limit, 0);
     assert.strictEqual(remote.requestAccountLines({
       account: ADDRESS, limit: -1
     }).message.limit, 0);
     assert.strictEqual(remote.requestAccountLines({
       account: ADDRESS, limit: -1e9
     }).message.limit, 0);
     assert.strictEqual(remote.requestAccountLines({
       account: ADDRESS, limit: -1e24
     }).message.limit, 0);
   });
   it('Construct account_lines request -- with max limit', function() {
     assert.strictEqual(remote.requestAccountLines({
       account: ADDRESS, limit: 1e9
     }).message.limit, 1e9);
     assert.strictEqual(remote.requestAccountLines({
       account: ADDRESS, limit: 1e9 + 1
     }).message.limit, 1e9);
     assert.strictEqual(remote.requestAccountLines({
       account: ADDRESS, limit: 1e10
     }).message.limit, 1e9);
     assert.strictEqual(remote.requestAccountLines({
       account: ADDRESS, limit: 1e24
     }).message.limit, 1e9);
   });

   it('Construct account_lines request -- with marker -- missing ledger',
      function() {
     assert.throws(function() {
       remote.requestAccountLines({account: ADDRESS, marker: PAGING_MARKER});
     }, 'A ledger_index or ledger_hash must be provided when using a marker');

     assert.throws(function() {
       remote.requestAccountLines({
         account: ADDRESS,
         marker: PAGING_MARKER,
         ledger: 'validated'
       });
     }, 'A ledger_index or ledger_hash must be provided when using a marker');

     assert.throws(function() {
       remote.requestAccountLines({
         account: ADDRESS,
         marker: PAGING_MARKER,
         ledger: NaN
       });
     }, 'A ledger_index or ledger_hash must be provided when using a marker');

     assert.throws(function() {
       remote.requestAccountLines({
         account: ADDRESS,
         marker: PAGING_MARKER,
         ledger: LEDGER_HASH.substr(0, 63)
       });
     }, 'A ledger_index or ledger_hash must be provided when using a marker');

     assert.throws(function() {
       remote.requestAccountLines({
         account: ADDRESS, marker: PAGING_MARKER, ledger: LEDGER_HASH + 'F'
       });
     }, 'A ledger_index or ledger_hash must be provided when using a marker');
   });
   it('Construct account_lines request -- with callback', function() {
     const request = remote.requestAccountLines({
       account: ADDRESS
     }, callback);

     assert.deepEqual(request.message, {
       command: 'account_lines',
       id: undefined,
       account: ADDRESS
     });
   });

   it('Construct account_tx request', function() {
     let request = remote.requestAccountTransactions({
       account: UInt160.ACCOUNT_ONE,
       ledger_index_min: -1,
       ledger_index_max: -1,
       limit: 5,
       forward: true,
       marker: PAGING_MARKER
     });

     assert.deepEqual(request.message, {
       command: 'account_tx',
       id: undefined,
       account: UInt160.ACCOUNT_ONE,
       ledger_index_min: -1,
       ledger_index_max: -1,
       binary: true,
       forward: true,
       limit: 5,
       marker: PAGING_MARKER
     });

     request = remote.requestAccountTransactions({
       account: UInt160.ACCOUNT_ONE,
       min_ledger: -1,
       max_ledger: -1
     });
     assert.deepEqual(request.message, {
       command: 'account_tx',
       id: undefined,
       account: UInt160.ACCOUNT_ONE,
       binary: true,
       ledger_index_min: -1,
       ledger_index_max: -1
     });
   });
   it('Construct account_tx request -- no binary', function() {
     const request = remote.requestAccountTransactions({
       account: UInt160.ACCOUNT_ONE,
       ledger_index_min: -1,
       ledger_index_max: -1,
       limit: 5,
       forward: true,
       binary: false,
       marker: PAGING_MARKER
     });

     assert.deepEqual(request.message, {
       command: 'account_tx',
       id: undefined,
       account: UInt160.ACCOUNT_ONE,
       ledger_index_min: -1,
       ledger_index_max: -1,
       binary: false,
       forward: true,
       limit: 5,
       marker: PAGING_MARKER
     });
   });

   it('Construct account_offers request -- no binary', function() {
     const request = remote.requestAccountOffers({account: ADDRESS});
     assert.deepEqual(request.message, {
       command: 'account_offers',
       id: undefined,
       account: ADDRESS
     });
   });


  it('Construct offer request -- with ledger index', function() {
    const request = remote.requestOffer({
      index: TRANSACTION_HASH, ledger: LEDGER_INDEX
    });
    assert.strictEqual(request.message.command, 'ledger_entry');
    assert.strictEqual(request.message.offer, TRANSACTION_HASH);
    assert.strictEqual(request.message.ledger_index, LEDGER_INDEX);
  });
  it('Construct offer request -- with ledger index and sequence', function() {
    const request = remote.requestOffer({
      account: ADDRESS, ledger: LEDGER_INDEX, sequence: 5
    });
    assert.strictEqual(request.message.command, 'ledger_entry');
    assert.strictEqual(request.message.offer.account, ADDRESS);
    assert.strictEqual(request.message.offer.seq, 5);
    assert.strictEqual(request.message.ledger_index, LEDGER_INDEX);
  });
  it('Construct offer request -- with ledger hash', function() {
    const request = remote.requestOffer({
      account: ADDRESS, ledger: LEDGER_HASH, sequence: 5
    });
    assert.strictEqual(request.message.command, 'ledger_entry');
    assert.strictEqual(request.message.offer.account, ADDRESS);
    assert.strictEqual(request.message.offer.seq, 5);
    assert.strictEqual(request.message.ledger_hash, LEDGER_HASH);
  });
  it('Construct offer request -- with ledger identifier and sequence',
     function() {
    const request = remote.requestOffer({
      account: ADDRESS, ledger: 'validated', sequence: 5
    });
    assert.strictEqual(request.message.command, 'ledger_entry');
    assert.strictEqual(request.message.offer.account, ADDRESS);
    assert.strictEqual(request.message.offer.seq, 5);
    assert.strictEqual(request.message.ledger_index, 'validated');
  });

  it('Construct book_offers request', function() {
    const request = remote.requestBookOffers({
      taker_gets: {
        currency: 'USD',
        issuer: ADDRESS
      },
      taker_pays: {
        currency: 'XRP'
      }
    });

    assert.deepEqual(request.message, {
      command: 'book_offers',
      id: undefined,
      taker_gets: {
        currency: Currency.from_human('USD').to_hex(),
        issuer: ADDRESS
      },
      taker_pays: {
        currency: Currency.from_human('XRP').to_hex()
      },
      taker: UInt160.ACCOUNT_ONE
    });
  });

  it('Construct book_offers request -- with ledger and limit', function() {
    const request = remote.requestBookOffers({
      taker_gets: {
        currency: 'USD',
        issuer: ADDRESS
      },
      taker_pays: {
        currency: 'XRP'
      },
      ledger: LEDGER_HASH,
      limit: 10
    });

    assert.deepEqual(request.message, {
      command: 'book_offers',
      id: undefined,
      taker_gets: {
        currency: Currency.from_human('USD').to_hex(),
        issuer: ADDRESS
      },
      taker_pays: {
        currency: Currency.from_human('XRP').to_hex()
      },
      taker: UInt160.ACCOUNT_ONE,
      ledger_hash: LEDGER_HASH,
      limit: 10
    });
  });

  it('Construct tx request', function() {
    const request = remote.requestTransaction({
      hash: TRANSACTION_HASH
    });

    assert.deepEqual(request.message, {
      command: 'tx',
      id: undefined,
      binary: true,
      transaction: TRANSACTION_HASH
    });
  });
  it('Construct tx request -- no binary', function() {
    const request = remote.requestTransaction({
      hash: TRANSACTION_HASH,
      binary: false
    });

    assert.deepEqual(request.message, {
      command: 'tx',
      id: undefined,
      binary: false,
      transaction: TRANSACTION_HASH
    });
  });

  it('Construct transaction_entry request', function() {
    const request = remote.requestTransactionEntry({
      hash: TRANSACTION_HASH
    });

    assert.deepEqual(request.message, {
      command: 'transaction_entry',
      id: undefined,
      tx_hash: TRANSACTION_HASH,
      ledger_index: 'validated'
    });
  });
  it('Construct transaction_entry request -- with ledger index', function() {
    const request = remote.requestTransactionEntry({
      hash: TRANSACTION_HASH,
      ledger: 1
    });

    assert.deepEqual(request.message, {
      command: 'transaction_entry',
      id: undefined,
      tx_hash: TRANSACTION_HASH,
      ledger_index: 1
    });
  });
  it('Construct transaction_entry request -- with ledger hash', function() {
    const request = remote.requestTransactionEntry({
      hash: TRANSACTION_HASH,
      ledger: LEDGER_HASH
    });

    assert.deepEqual(request.message, {
      command: 'transaction_entry',
      id: undefined,
      tx_hash: TRANSACTION_HASH,
      ledger_hash: LEDGER_HASH
    });
  });
  it('Construct transaction_entry request -- with invalid ledger', function() {
    assert.throws(function() {
      remote.requestTransactionEntry({
        hash: TRANSACTION_HASH,
        ledger: {}
      });
    });
  });

  it('Construct tx_history request', function() {
    const request = remote.requestTransactionHistory({
      start: 1
    });

    assert.deepEqual(request.message, {
      command: 'tx_history',
      id: undefined,
      start: 1
    });
  });

  it('Construct wallet_accounts request', function() {
    const request = remote.requestWalletAccounts({
      seed: 'shmnpxY42DaoyNbNQDoGuymNT1T9U'
    });

    assert.deepEqual(request.message, {
      command: 'wallet_accounts',
      id: undefined,
      seed: 'shmnpxY42DaoyNbNQDoGuymNT1T9U'
    });
  });
  it('Construct wallet_accounts request -- untrusted', function() {
    remote.trusted = false;

    assert.throws(function() {
      remote.requestWalletAccounts({
        seed: 'shmnpxY42DaoyNbNQDoGuymNT1T9U'
      });
    });
  });

  it('Construct sign request', function() {
    const request = remote.requestSign({
      secret: 'shmnpxY42DaoyNbNQDoGuymNT1T9U',
      tx_json: {
        Flags: 0,
        TransactionType: 'AccountSet',
        Account: 'rwLZs9MUVv28XZdYXDk9uNRUpAh1c6jij8'
      }
    });

    assert.deepEqual(request.message, {
      command: 'sign',
      id: undefined,
      secret: 'shmnpxY42DaoyNbNQDoGuymNT1T9U',
      tx_json: {
        Flags: 0,
        TransactionType: 'AccountSet',
        Account: 'rwLZs9MUVv28XZdYXDk9uNRUpAh1c6jij8'
      }
    });
  });
  it('Construct sign request -- untrusted', function() {
    remote.trusted = false;

    assert.throws(function() {
      remote.requestSign({
        secret: 'shmnpxY42DaoyNbNQDoGuymNT1T9U',
        tx_json: {
          Flags: 0,
          TransactionType: 'AccountSet',
          Account: 'rwLZs9MUVv28XZdYXDk9uNRUpAh1c6jij8'
        }
      });
    });
  });

  it('Construct submit request', function() {
    const request = remote.requestSubmit();
    assert.deepEqual(request.message, {
      command: 'submit',
      id: undefined
    });
  });

  it('Construct transaction', function() {
    let tx = remote.createTransaction('AccountSet', {
      account: 'rwLZs9MUVv28XZdYXDk9uNRUpAh1c6jij8',
      flags: 0
    });
    assert(tx instanceof Transaction);
    assert.deepEqual(tx.tx_json, {
      Flags: 0,
      TransactionType: 'AccountSet',
      Account: 'rwLZs9MUVv28XZdYXDk9uNRUpAh1c6jij8'
    });

    tx = remote.createTransaction();
    assert(tx instanceof Transaction);
    assert.deepEqual(tx.tx_json, {
      Flags: 0
    });
  });
  it('Construct transaction -- invalid type', function() {
    assert.throws(function() {
      remote.createTransaction('AccountSetz', {
        account: 'rwLZs9MUVv28XZdYXDk9uNRUpAh1c6jij8',
        flags: 0
      });
    });
  });

  it('Construct ledger_accept request', function() {
    remote._stand_alone = true;
    const request = remote.requestLedgerAccept();

    assert.deepEqual(request.message, {
      command: 'ledger_accept',
      id: undefined
    });

    remote._servers[0].emit('connect');
    remote._servers[0].emit('message', {
      type: 'ledgerClosed',
      fee_base: 10,
      fee_ref: 10,
      ledger_hash:
        'F824560DD788E5E4B65F5843A6616872873EAB74AA759C73A992355FFDFC4237',
      ledger_index: 11368614,
      ledger_time: 475696280,
      reserve_base: 20000000,
      reserve_inc: 5000000,
      txn_count: 9,
      validated_ledgers: '32570-11368614'
    });
  });
  it('Construct ledger_accept request -- not standalone', function() {
    assert.throws(function() {
      remote.requestLedgerAccept();
    });
  });

  it('Construct ripple balance request', function() {
    const request = remote.requestRippleBalance({
      account: 'rGr9PjmVe7MqEXTSbd3njhgJc2s5vpHV54',
      issuer: 'rwxBjBC9fPzyQ9GgPZw6YYLNeRTSx5c2W6',
      ledger: 1,
      currency: 'USD'
    });

    assert.deepEqual(request.message, {
      command: 'ledger_entry',
      id: undefined,
      ripple_state: {
        currency: 'USD',
        accounts: [
          'rGr9PjmVe7MqEXTSbd3njhgJc2s5vpHV54',
          'rwxBjBC9fPzyQ9GgPZw6YYLNeRTSx5c2W6'
        ]
      },
      ledger_index: 1
    });
  });

  it('Construct ripple_path_find request', function() {
    const request = remote.requestRipplePathFind({
      src_account: 'rGr9PjmVe7MqEXTSbd3njhgJc2s5vpHV54',
      dst_account: 'rwxBjBC9fPzyQ9GgPZw6YYLNeRTSx5c2W6',
      dst_amount: '1/USD/rGr9PjmVe7MqEXTSbd3njhgJc2s5vpHV54',
      src_currencies: [{
        currency: 'BTC', issuer: 'rwxBjBC9fPzyQ9GgPZw6YYLNeRTSx5c2W6'
      }]
    });

    assert.deepEqual(request.message, {
      command: 'ripple_path_find',
      id: undefined,
      source_account: 'rGr9PjmVe7MqEXTSbd3njhgJc2s5vpHV54',
      destination_account: 'rwxBjBC9fPzyQ9GgPZw6YYLNeRTSx5c2W6',
      destination_amount: {
        value: '1',
        currency: 'USD',
        issuer: 'rGr9PjmVe7MqEXTSbd3njhgJc2s5vpHV54'
      },
      source_currencies: [{
        issuer: 'rwxBjBC9fPzyQ9GgPZw6YYLNeRTSx5c2W6',
        currency: '0000000000000000000000004254430000000000'
      }]
    });
  });

  it('createPathFind', function() {
    const servers = [
      makeServer('wss://localhost:5006'),
      makeServer('wss://localhost:5007')
    ];

    remote._servers = servers;

    const pathfind = remote.createPathFind({
      src_account: 'rGr9PjmVe7MqEXTSbd3njhgJc2s5vpHV54',
      dst_account: 'rwxBjBC9fPzyQ9GgPZw6YYLNeRTSx5c2W6',
      dst_amount: '1/USD/rGr9PjmVe7MqEXTSbd3njhgJc2s5vpHV54',
      src_currencies: [{
        currency: 'BTC', issuer: 'rwxBjBC9fPzyQ9GgPZw6YYLNeRTSx5c2W6'
      }]
    });

    // TODO: setup a mock server to provide a response
    pathfind.on('update', message => console.log(message));
  });

  it('Construct path_find create request', function() {
    const request = remote.requestPathFindCreate({
      src_account: 'rGr9PjmVe7MqEXTSbd3njhgJc2s5vpHV54',
      dst_account: 'rwxBjBC9fPzyQ9GgPZw6YYLNeRTSx5c2W6',
      dst_amount: '1/USD/rGr9PjmVe7MqEXTSbd3njhgJc2s5vpHV54',
      src_currencies: [{
        currency: 'BTC', issuer: 'rwxBjBC9fPzyQ9GgPZw6YYLNeRTSx5c2W6'
      }]
    });

    assert.deepEqual(request.message, {
      command: 'path_find',
      id: undefined,
      subcommand: 'create',
      source_account: 'rGr9PjmVe7MqEXTSbd3njhgJc2s5vpHV54',
      destination_account: 'rwxBjBC9fPzyQ9GgPZw6YYLNeRTSx5c2W6',
      destination_amount: {
        value: '1',
        currency: 'USD',
        issuer: 'rGr9PjmVe7MqEXTSbd3njhgJc2s5vpHV54'
      },
      source_currencies: [{
        issuer: 'rwxBjBC9fPzyQ9GgPZw6YYLNeRTSx5c2W6',
        currency: '0000000000000000000000004254430000000000'
      }]
    });
  });

  it('Construct path_find close request', function() {
    const request = remote.requestPathFindClose();

    assert.deepEqual(request.message, {
      command: 'path_find',
      id: undefined,
      subcommand: 'close'
    });
  });

  it('Construct Payment transaction', function() {
    const tx = remote.createTransaction('Payment', {
      account: TX_JSON.Account,
      destination: TX_JSON.Destination,
      amount: TX_JSON.Amount
    });

    assert.deepEqual(tx.tx_json, {
      Flags: 0,
      TransactionType: 'Payment',
      Account: TX_JSON.Account,
      Destination: TX_JSON.Destination,
      Amount: TX_JSON.Amount
    });
  });
  it('Construct AccountSet transaction', function() {
    const tx = remote.createTransaction('AccountSet', {
      account: TX_JSON.Account,
      set: 'asfRequireDest'
    });

    assert.deepEqual(tx.tx_json, {
      Flags: 0,
      TransactionType: 'AccountSet',
      Account: TX_JSON.Account,
      SetFlag: 1
    });
  });
  it('Construct TrustSet transaction', function() {
    const tx = remote.createTransaction('TrustSet', {
      account: TX_JSON.Account,
      limit: '1/USD/' + TX_JSON.Destination
    });

    assert.deepEqual(tx.tx_json, {
      Flags: 0,
      TransactionType: 'TrustSet',
      Account: TX_JSON.Account,
      LimitAmount: {
        value: '1',
        currency: 'USD',
        issuer: TX_JSON.Destination
      }
    });
  });
  it('Construct OfferCreate transaction', function() {
    const tx = remote.createTransaction('OfferCreate', {
      account: TX_JSON.Account,
      taker_gets: '1/USD/' + TX_JSON.Destination,
      taker_pays: '1/BTC/' + TX_JSON.Destination
    });

    assert.deepEqual(tx.tx_json, {
      Flags: 0,
      TransactionType: 'OfferCreate',
      Account: TX_JSON.Account,
      TakerGets: {
        value: '1',
        currency: 'USD',
        issuer: TX_JSON.Destination
      },
      TakerPays: {
        value: '1',
        currency: 'BTC',
        issuer: TX_JSON.Destination
      }
    });
  });
  it('Construct OfferCancel transaction', function() {
    const tx = remote.createTransaction('OfferCancel', {
      account: TX_JSON.Account,
      offer_sequence: 1
    });

    assert.deepEqual(tx.tx_json, {
      Flags: 0,
      TransactionType: 'OfferCancel',
      Account: TX_JSON.Account,
      OfferSequence: 1
    });
  });
  it('Construct SetRegularKey transaction', function() {
    const tx = remote.createTransaction('SetRegularKey', {
      account: TX_JSON.Account,
      regular_key: TX_JSON.Destination
    });

    assert.deepEqual(tx.tx_json, {
      Flags: 0,
      TransactionType: 'SetRegularKey',
      Account: TX_JSON.Account,
      RegularKey: TX_JSON.Destination
    });
  });
});
