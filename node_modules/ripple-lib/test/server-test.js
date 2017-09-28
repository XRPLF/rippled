'use strict';

/* eslint-disable no-new */

const _ = require('lodash');
const assert = require('assert');
const ws = require('ws');
const Remote = require('ripple-lib').Remote;
const Server = require('ripple-lib').Server;
const Request = require('ripple-lib').Request;

describe('Server', function() {
  it('Server constructor - invalid options', function() {
    assert.throws(function() {
      new Server(new Remote());
    });
  });

  it('Message listener', function(done) {
    const server = new Server(new Remote(), 'wss://localhost:5006');

    server._handleMessage = function(message) {
      assert.strictEqual(typeof message, 'string');
      assert.deepEqual(JSON.parse(message).result, {});
      done();
    };

    server.emit('message', JSON.stringify({
      result: {}
    }));
  });

  it('Subscribe response listener', function(done) {
    const server = new Server(new Remote(), 'wss://localhost:5006');

    server._handleResponseSubscribe = function(message) {
      assert.strictEqual(typeof message, 'string');
      assert.deepEqual(JSON.parse(message).result, {});
      done();
    };

    server.emit('response_subscribe', JSON.stringify({
      result: {}
    }));
  });

  it('Activity listener', function(done) {
    // Activity listener should be enabled
    const server = new Server(new Remote(), 'wss://localhost:5006');

    server.emit('ledger_closed');

    const interval = setInterval(function() {}, Infinity);

    assert.deepEqual(Object.getPrototypeOf(server._activityInterval),
                     Object.getPrototypeOf(interval));

    clearInterval(interval);

    done();
  });

  it('Reconnect activity listener', function(done) {
    const server = new Server(new Remote(), 'wss://localhost:5006');

    server.emit('ledger_closed');

    const interval = setInterval(function() {}, Infinity);

    assert.deepEqual(Object.getPrototypeOf(server._activityInterval),
                     Object.getPrototypeOf(interval));

    server.once('disconnect', function() {
      // Interval should clear
      assert.deepEqual(Object.getPrototypeOf(server._activityInterval),
                       Object.getPrototypeOf(interval));
      assert.strictEqual(server._activityInterval._onTimeout, null);

      server.once('ledger_closed', function() {
        // Interval should be reset
        assert.deepEqual(Object.getPrototypeOf(server._activityInterval),
                         Object.getPrototypeOf(interval));
        assert.strictEqual(typeof server._activityInterval._onTimeout,
                           'function');
        done();
      });

      server.emit('ledger_closed');
    });

    server.emit('disconnect');
  });

  it('Update server score ledger close listener', function(done) {
    const server = new Server(new Remote(), 'wss://localhost:5006');

    const ledger = {
      type: 'ledgerClosed',
      fee_base: 10,
      fee_ref: 10,
      ledger_hash:
        'D29E1F2A2617A88E9DAA14F468B169E6875092ECA0B3B1FA2BE1BC5524DE7CB2',
      ledger_index: 7035609,
      ledger_time: 455327690,
      reserve_base: 20000000,
      reserve_inc: 5000000,
      txn_count: 1,
      validated_ledgers: '32570-7035609'
    };

    server._updateScore = function(type, data) {
      assert.strictEqual(type, 'ledgerclose');
      assert.deepEqual(data, ledger);
      done();
    };

    server._remote.emit('ledger_closed', ledger);
  });

  it('Update server score ping listener', function(done) {
    const server = new Server(new Remote(), 'wss://localhost:5006');

    const ping = {
      time: 500
    };

    server._updateScore = function(type, data) {
      assert.strictEqual(type, 'response');
      assert.deepEqual(data, ping);
      done();
    };

    server.emit('response_ping', {}, ping);
  });

  it('Update server score load listener', function(done) {
    const server = new Server(new Remote(), 'wss://localhost:5006');

    const load = {
      fee_base: 10,
      fee_ref: 10
    };

    server._updateScore = function(type, data) {
      assert.strictEqual(type, 'loadchange');
      assert.deepEqual(data, load);
      done();
    };

    server.emit('load_changed', load);
  });

  it('Websocket constructor', function() {
    assert.strictEqual(Server.websocketConstructor(), require('ws'));
  });

  it('Set state online', function(done) {
    const server = new Server(new Remote(), 'wss://localhost:5006');

    server._state = 'offline';

    server.once('connect', function() {
      assert(server._connected);
      done();
    });

    server._setState('online');
  });

  it('Set state offline', function(done) {
    const server = new Server(new Remote(), 'wss://localhost:5006');

    server._state = 'online';

    server.once('disconnect', function() {
      assert(!server._connected);
      done();
    });

    server._setState('offline');
  });

  it('Set state same state', function(done) {
    const server = new Server(new Remote(), 'wss://localhost:5006');

    server._state = 'online';

    server.once('state', function(state) {
      assert(!server._connected);
      assert.strictEqual(state, 'offline');
      done();
    });

    server._setState('online');
    server._setState('offline');
  });

  it('Check activity - inactive', function(done) {
    const server = new Server(new Remote(), 'wss://localhost:5006');
    server._connected = true;

    server.reconnect = function() {
      done();
    };

    server._lastLedgerClose = Date.now() - 1000 * 30;
    server._checkActivity();
  });

  it('Check activity - unconnected', function(done) {
    const server = new Server(new Remote(), 'wss://localhost:5006');
    server._connected = false;

    server.reconnect = function() {
      assert(false, 'Should not reconnect');
    };

    server._lastLedgerClose = Date.now() - 1000 * 30;
    server._checkActivity();
    setImmediate(function() {
      done();
    });
  });

  it('Check activity - uninitialized', function(done) {
    const server = new Server(new Remote(), 'wss://localhost:5006');
    server._connected = false;

    server.reconnect = function() {
      assert(false, 'Should not reconnect');
    };

    // server._lastLedgerClose = Date.now() - 1000 * 30;
    server._checkActivity();
    setImmediate(function() {
      done();
    });
  });

  it('Check activity - sufficient ledger close', function(done) {
    const server = new Server(new Remote(), 'wss://localhost:5006');
    server._connected = false;

    server.reconnect = function() {
      assert(false, 'Should not reconnect');
    };

    server._lastLedgerClose = Date.now() - 1000 * 20;
    server._checkActivity();
    setImmediate(function() {
      done();
    });
  });

  it('Update score - response', function() {
    const server = new Server(new Remote(), 'wss://localhost:5006');
    server._connected = true;

    assert.deepEqual(server._scoreWeights, {
      ledgerclose: 5,
      response: 1
    });

    assert.strictEqual(server._score, 0);

    server._updateScore('response', {
      time: Date.now() - 1000
    });

    // 1000ms second ping / 200ms * weight of 1
    assert.strictEqual(server._score, 5);
  });

  it('Update score - ledger', function() {
    const server = new Server(new Remote(), 'wss://localhost:5006');
    server._connected = true;
    server._lastLedgerIndex = 1;

    assert.deepEqual(server._scoreWeights, {
      ledgerclose: 5,
      response: 1
    });

    assert.strictEqual(server._score, 0);

    server._updateScore('ledgerclose', {
      ledger_index: 5
    });

    // Four ledgers behind the leading ledger * weight of 5
    assert.strictEqual(server._score, 20);
  });

  it('Update score - load', function() {
    const server = new Server(new Remote(), 'wss://localhost:5006');
    server._connected = true;
    server._fee_cushion = 1;

    assert.deepEqual(server._scoreWeights, {
      ledgerclose: 5,
      response: 1
    });

    assert.strictEqual(server._fee, 10);

    server.emit('message', {
      type: 'serverStatus',
      load_base: 256,
      load_factor: 256 * 10,
      server_status: 'full'
    });

    // server._updateScore('loadchange', { });

    assert.strictEqual(server._fee, 100);
  });

  it('Update score - reaching reconnect threshold', function(done) {
    const server = new Server(new Remote(), 'wss://localhost:5006');
    server._lastLedgerIndex = 1;
    server._connected = true;

    server.reconnect = function() {
      done();
    };

    assert.deepEqual(server._scoreWeights, {
      ledgerclose: 5,
      response: 1
    });

    assert.strictEqual(server._score, 0);

    server._updateScore('ledgerclose', {
      ledger_index: 250
    });

    // Four ledgers behind the leading ledger * weight of 5
    assert.strictEqual(server._score, 1245);
  });

  it('Get remote address', function() {
    const server = new Server(new Remote(), 'wss://localhost:5006');
    server._connected = true;
    server._ws = {
      _socket: {
        remoteAddress: '127.0.0.1'
      }
    };
    assert.strictEqual(server._remoteAddress(), '127.0.0.1');
  });

  it('Disconnect', function(done) {
    const server = new Server(new Remote(), 'wss://localhost:5006');
    server._connected = true;

    server._ws = {
      close: function() {
        assert(!server._shouldConnect);
        assert.strictEqual(server._state, 'offline');
        done();
      }
    };

    server.disconnect();
  });

  it('Connect', function(done) {
    const wss = new ws.Server({
      port: 5748
    });

    wss.once('connection', function(_ws) {
      _ws.once('message', function(message) {
        const m = JSON.parse(message);

        assert.deepEqual(m, {
          command: 'subscribe',
          id: 0,
          streams: ['ledger', 'server']
        });

        _ws.send(JSON.stringify({
          id: 0,
          status: 'success',
          type: 'response',
          result: {
            fee_base: 10,
            fee_ref: 10,
            ledger_hash:
            '1838539EE12463C36F2C53B079D807C697E3D93A1936B717E565A4A912E11776',
            ledger_index: 7053695,
            ledger_time: 455414390,
            load_base: 256,
            load_factor: 256,
            random:
            'E56C9154D9BE94D49C581179356C2E084E16D18D74E8B09093F2D61207625E6A',
            reserve_base: 20000000,
            reserve_inc: 5000000,
            server_status: 'full',
            validated_ledgers: '32570-7053695'
          }
        }));
      });
    });

    const server = new Server(new Remote(), 'ws://localhost:5748');

    server.once('connect', function() {
      server.once('disconnect', function() {
        wss.close();
        done();
      });
      server.disconnect();
    });

    server.connect();
  });

  it('Connect - already connected', function(done) {
    const server = new Server(new Remote(), 'ws://localhost:5748');
    server._connected = true;

    server.once('connect', function() {
      assert(false, 'Should not connect');
    });

    server.connect();

    setImmediate(function() {
      done();
    });
  });

  it.skip('Connect - prior WebSocket connection exists', function(done) {
    const wss = new ws.Server({
      port: 5748
    });

    wss.once('connection', function(_ws) {
      _ws.once('message', function(message) {
        const m = JSON.parse(message);

        assert.deepEqual(m, {
          command: 'subscribe',
          id: 0,
          streams: ['ledger', 'server']
        });

        _ws.send(JSON.stringify({
          id: 0,
          status: 'success',
          type: 'response',
          result: {
            fee_base: 10,
            fee_ref: 10,
            ledger_hash:
            '1838539EE12463C36F2C53B079D807C697E3D93A1936B717E565A4A912E11776',
            ledger_index: 7053695,
            ledger_time: 455414390,
            load_base: 256,
            load_factor: 256,
            random:
            'E56C9154D9BE94D49C581179356C2E084E16D18D74E8B09093F2D61207625E6A',
            reserve_base: 20000000,
            reserve_inc: 5000000,
            server_status: 'full',
            validated_ledgers: '32570-7053695'
          }
        }));
      });
    });

    const server = new Server(new Remote(), 'ws://localhost:5748');

    server.once('connect', function() {
      server.once('disconnect', function() {
        wss.close();
        done();
      });
      server.disconnect();
    });

    server.connect();
    server.connect();
  });

  it('Connect - no WebSocket constructor', function() {
    const server = new Server(new Remote(), 'ws://localhost:5748');
    server._connected = false;

    const websocketConstructor = Server.websocketConstructor;

    Server.websocketConstructor = function() {
      return undefined;
    };

    assert.throws(function() {
      server.connect();
    }, Error);

    Server.websocketConstructor = websocketConstructor;
  });

  it('Connect - partial history disabled', function(done) {
    const wss = new ws.Server({
      port: 5748
    });

    wss.once('connection', function(_ws) {
      _ws.once('message', function(message) {
        const m = JSON.parse(message);

        assert.deepEqual(m, {
          command: 'subscribe',
          id: 0,
          streams: ['ledger', 'server']
        });

        _ws.send(JSON.stringify({
          id: 0,
          status: 'success',
          type: 'response',
          result: {
            fee_base: 10,
            fee_ref: 10,
            ledger_hash:
            '1838539EE12463C36F2C53B079D807C697E3D93A1936B717E565A4A912E11776',
            ledger_index: 7053695,
            ledger_time: 455414390,
            load_base: 256,
            load_factor: 256,
            random:
            'E56C9154D9BE94D49C581179356C2E084E16D18D74E8B09093F2D61207625E6A',
            reserve_base: 20000000,
            reserve_inc: 5000000,
            server_status: 'syncing',
            validated_ledgers: '3175520-3176615'
          }
        }));
      });
    });

    const server = new Server(new Remote({
      allow_partial_history: false
    }), 'ws://localhost:5748');

    server.reconnect = function() {
      setImmediate(function() {
        wss.close();
        done();
      });
    };

    server.once('connect', function() {
      assert(false, 'Should not connect');
    });

    server.connect();
  });

  it('Connect - syncing state', function(done) {
    // Test that fee and load defaults are not overwritten by
    // undefined properties on server subscribe response
    const wss = new ws.Server({
      port: 5748
    });

    wss.once('connection', function(_ws) {
      _ws.once('message', function(message) {
        const m = JSON.parse(message);

        assert.deepEqual(m, {
          command: 'subscribe',
          id: 0,
          streams: ['ledger', 'server']
        });

        _ws.send(JSON.stringify({
          id: 0,
          status: 'success',
          type: 'response',
          result: {
            load_base: 256,
            load_factor: 256,
            server_status: 'syncing'
          }
        }));
      });
    });

    const server = new Server(new Remote(), 'ws://localhost:5748');

    server.once('connect', function() {
      assert(server.isConnected());
      assert.strictEqual(server._load_base, 256);
      assert.strictEqual(server._load_factor, 256);
      assert.strictEqual(server._fee_base, 10);
      assert.strictEqual(server._fee_ref, 10);
      wss.close();
      done();
    });

    server.connect();
  });


  it('Reconnect', function(done) {
    const server = new Server(new Remote(), 'ws://localhost:5748');
    server._connected = true;
    server._shouldConnect = true;
    server._ws = { };

    let disconnected = false;

    server.disconnect = function() {
      disconnected = true;
      server.emit('disconnect');
    };

    server.connect = function() {
      assert(disconnected);
      done();
    };

    server.reconnect();
  });

  it('Retry connect', function(done) {
    const server = new Server(new Remote(), 'ws://localhost:5748');
    server._connected = false;
    server._shouldConnect = true;

    server.connect = function() {
      done();
    };

    server._retryConnect();

    const timeout = setTimeout(function() {}, Infinity);

    assert.deepEqual(Object.getPrototypeOf(server._retryTimer),
                     Object.getPrototypeOf(timeout));

    clearTimeout(timeout);
  });

  it('Handle close', function() {
    const server = new Server(new Remote(), 'ws://localhost:5748');
    server._ws = { };
    server._handleClose();
    assert.strictEqual(server._ws.onopen, _.noop);
    assert.strictEqual(server._ws.onclose, _.noop);
    assert.strictEqual(server._ws.onmessage, _.noop);
    assert.strictEqual(server._ws.onerror, _.noop);
    assert.strictEqual(server._state, 'offline');
  });

  it('Handle error', function(done) {
    const wss = new ws.Server({
      port: 5748
    });

    wss.once('connection', function(_ws) {
      _ws.once('message', function(message) {
        const m = JSON.parse(message);

        assert.deepEqual(m, {
          command: 'subscribe',
          id: 0,
          streams: ['ledger', 'server']
        });

        _ws.send(JSON.stringify({
          id: 0,
          status: 'success',
          type: 'response',
          result: {
            fee_base: 10,
            fee_ref: 10,
            ledger_hash:
            '1838539EE12463C36F2C53B079D807C697E3D93A1936B717E565A4A912E11776',
            ledger_index: 7053695,
            ledger_time: 455414390,
            load_base: 256,
            load_factor: 256,
            random:
            'E56C9154D9BE94D49C581179356C2E084E16D18D74E8B09093F2D61207625E6A',
            reserve_base: 20000000,
            reserve_inc: 5000000,
            server_status: 'full',
            validated_ledgers: '32570-7053695'
          }
        }));
      });
    });

    const server = new Server(new Remote(), 'ws://localhost:5748');

    server.once('disconnect', function() {
      done();
    });

    server.once('connect', function() {
      server._retryConnect = function() {
        wss.close();
      };
      server._ws.emit('error', new Error());
    });

    server.connect();
  });

  it('Handle message - ledgerClosed', function(done) {
    const server = new Server(new Remote(), 'ws://localhost:5748');

    const ledger = {
      type: 'ledgerClosed',
      ledger_index: 1
    };

    server.once('ledger_closed', function() {
      assert.strictEqual(server._lastLedgerIndex, ledger.ledger_index);
      done();
    });

    server.emit('message', ledger);
  });

  it('Handle message - serverStatus', function(done) {
    const remote = new Remote();
    const server = new Server(remote, 'ws://localhost:5748');
    const events = 3;
    let receivedEvents = 0;

    const status = {
      type: 'serverStatus',
      load_base: 256,
      load_factor: 256 * 2
    };

    server.once('load', function(message) {
      assert.deepEqual(message, status);
      if (++receivedEvents === events) {
        done();
      }
    });

    server.once('load_changed', function(message) {
      assert.deepEqual(message, status);
      if (++receivedEvents === events) {
        done();
      }
    });

    remote.once('load_changed', function(message) {
      assert.deepEqual(message, status);
      if (++receivedEvents === events) {
        done();
      }
    });

    server.emit('message', status);
  });

  it('Handle message - serverStatus - no load', function(done) {
    const remote = new Remote();
    const server = new Server(remote, 'ws://localhost:5748');

    const status = {
      type: 'serverStatus'
    };

    server.once('load', function(message) {
      assert.deepEqual(message, status);
    });

    server.once('load_changed', function() {
      assert(false, 'Non-load status should not trigger events');
    });

    remote.once('load_changed', function() {
      assert(false, 'Non-load status should not trigger events');
    });

    server.emit('message', status);

    setImmediate(function() {
      done();
    });
  });

  it('Handle message - response - success', function(done) {
    const remote = new Remote();
    const server = new Server(remote, 'ws://localhost:5748');
    const request = new Request(remote, 'server_info');
    const id = 1;

    assert(request instanceof process.EventEmitter);

    server._requests[id] = request;

    const response = {
      id: id,
      type: 'response',
      status: 'success',
      result: {
        info: {
          build_version: '0.25.2-rc1',
          complete_ledgers: '32570-7623483',
          hostid: 'MAC',
          io_latency_ms: 1,
          last_close: {
            converge_time_s: 2.052,
            proposers: 5
          },
          load_factor: 1,
          peers: 50,
          pubkey_node: 'n94pSqypSfddzAVj9qoezHyUoetsrMnwgNuBqRJ3WHvM8aMMf7rW',
          server_state: 'full',
          validated_ledger: {
            age: 5,
            base_fee_xrp: 0.00001,
            hash:
            'AB575193C623179078BE7CC42965FD4262EE8611D1CE7F839CEEBFFEF4B653B6',
            reserve_base_xrp: 20,
            reserve_inc_xrp: 5,
            seq: 7623483
          },
          validation_quorum: 3
        }
      }
    };

    let receivedEvents = 0;
    const emitters = 3;

    request.once('success', function(message) {
      assert.deepEqual(message, response.result);
      if (++receivedEvents === emitters) {
        done();
      }
    });

    server.once('response_server_info', function(message) {
      assert.deepEqual(message, response.result);
      if (++receivedEvents === emitters) {
        done();
      }
    });

    remote.once('response_server_info', function(message) {
      assert.deepEqual(message, response.result);
      if (++receivedEvents === emitters) {
        done();
      }
    });

    server.emit('message', response);
  });

  it('Handle message - response - error', function(done) {
    const remote = new Remote();
    const server = new Server(remote, 'ws://localhost:5748');
    const request = new Request(remote, 'server_info');
    const id = 1;

    assert(request instanceof process.EventEmitter);

    server._requests[id] = request;

    const response = {
      id: id,
      type: 'response',
      status: 'error',
      error: {
        test: 'property'
      }
    };

    request.once('error', function(message) {
      assert.deepEqual(message, {
        error: 'remoteError',
        error_message: 'Remote reported an error.',
        remote: response
      });
      done();
    });

    server.emit('message', response);
  });

  it('Handle message - response - no request', function(done) {
    const remote = new Remote();
    const server = new Server(remote, 'ws://localhost:5748');

    const response = {
      id: 1,
      type: 'response',
      status: 'success',
      result: { }
    };

    Object.defineProperty(response, 'status', {
      get: function() {
        assert(false, 'Response status should not be checked');
      }
    });

    server.emit('message', response);

    setImmediate(function() {
      done();
    });
  });

  it('Handle message - path_find', function(done) {
    const server = new Server(new Remote(), 'ws://localhost:5748');

    server._handlePathFind = function() {
      done();
    };

    server.emit('message', {
      type: 'path_find'
    });
  });

  it('Handle message - invalid message', function(done) {
    const server = new Server(new Remote(), 'ws://localhost:5748');

    server.once('unexpected', function() {
      done();
    });

    server.emit('message', {
      butt: 'path_find'
    });
  });

  it('Send message', function(done) {
    const server = new Server(new Remote(), 'ws://localhost:5748');

    const request = {
      id: 1,
      message: {
        command: 'server_info'
      }
    };

    server._ws = {
      send: function(message) {
        assert.deepEqual(JSON.parse(message), request);
        done();
      }
    };

    server._sendMessage(request);
  });

  it('Request', function(done) {
    const server = new Server(new Remote(), 'ws://localhost:5748');
    server._connected = true;

    server._ws = { };

    const request = {
      message: {
        command: 'server_info'
      }
    };

    server._sendMessage = function() {
      done();
    };

    server._request(request);
  });

  it('Request - delayed connect', function(done) {
    const server = new Server(new Remote(), 'ws://localhost:5748');
    server._connected = false;

    server._ws = { };

    const request = {
      message: {
        command: 'server_info'
      }
    };

    server._request(request);

    setImmediate(function() {
      server._sendMessage = function() {
        done();
      };

      server.emit('connect');
    });
  });

  it('Request - no WebSocket', function(done) {
    const server = new Server(new Remote(), 'ws://localhost:5748');
    server._connected = true;

    server._ws = undefined;

    const request = {
      message: {
        command: 'server_info'
      }
    };

    server._sendMessage = function() {
      assert(false, 'Should not send message if WebSocket does not exist');
    };

    server._request(request);

    setImmediate(function() {
      done();
    });
  });

  it('Check connectivity', function() {
    const server = new Server(new Remote(), 'ws://localhost:5748');
    server._connected = false;
    server._ws = {
      readyState: 1
    };

    assert(!server._isConnected());

    server._connected = true;

    assert(server._isConnected());
  });

  it('Compute fee - fee units', function() {
    const server = new Server(new Remote(), 'ws://localhost:5748');
    assert.strictEqual(server._computeFee(10), '12');
  });

  it('Compute fee - bad arg', function() {
    const server = new Server(new Remote(), 'ws://localhost:5748');
    assert.throws(function() {
      server._computeFee('asdf');
    });
  });

  it('Compute fee - increased load', function() {
    const server = new Server(new Remote(), 'ws://localhost:5748');
    server._load_base = 256;
    server._load_factor = 256 * 4;
    assert.strictEqual(server._computeFee(10), '48');
  });

  it('Compute reserve', function() {
    const server = new Server(new Remote(), 'ws://localhost:5748');
    server._reserve_base = 20000000;
    server._reserve_inc = 5000000;
    assert.strictEqual(server._reserve().to_json(), '20000000');
  });

  it('Compute reserve, positive OwnerCount', function() {
    const server = new Server(new Remote(), 'ws://localhost:5748');
    server._reserve_base = 20000000;
    server._reserve_inc = 5000000;
    assert.strictEqual(server._reserve(4).to_json(), '40000000');
  });

  it('Cache hostid', function(done) {
    const wss = new ws.Server({
      port: 5748
    });

    wss.once('connection', function(_ws) {
      function sendServerInfo(message) {
        _ws.send(JSON.stringify({
          id: message.id,
          status: 'success',
          type: 'response',
          result: {
            info: {
              build_version: '0.25.2-rc1',
              complete_ledgers: '32570-7623483',
              hostid: 'MAC',
              io_latency_ms: 1,
              last_close: {
                converge_time_s: 2.052,
                proposers: 5
              },
              load_factor: 1,
              peers: 50,
              pubkey_node:
                'n94pSqypSfddzAVj9qoezHyUoetsrMnwgNuBqRJ3WHvM8aMMf7rW',
              server_state: 'full',
              validated_ledger: {
                age: 5,
                base_fee_xrp: 0.00001,
                hash: 'AB575193C623179078BE7CC42965FD4262EE8611D1CE7F839'
                      + 'CEEBFFEF4B653B6',
                reserve_base_xrp: 20,
                reserve_inc_xrp: 5,
                seq: 7623483
              },
              validation_quorum: 3
            }
          }
        }));
      }

      function sendSubscribe(message) {
        _ws.send(JSON.stringify({
          id: message.id,
          status: 'success',
          type: 'response',
          result: {
            fee_base: 10,
            fee_ref: 10,
            ledger_hash:
            '1838539EE12463C36F2C53B079D807C697E3D93A1936B717E565A4A912E11776',
            ledger_index: 7053695,
            ledger_time: 455414390,
            load_base: 256,
            load_factor: 256,
            random:
            'E56C9154D9BE94D49C581179356C2E084E16D18D74E8B09093F2D61207625E6A',
            reserve_base: 20000000,
            reserve_inc: 5000000,
            server_status: 'full',
            validated_ledgers: '32570-7053695'
          }
        }));
      }

      _ws.on('message', function(message) {
        const m = JSON.parse(message);

        switch (m.command) {
          case 'subscribe':
            assert.strictEqual(m.command, 'subscribe');
            assert.deepEqual(m.streams, ['ledger', 'server']);
            setImmediate(function() {
              sendSubscribe(m);
            });
            break;
          case 'server_info':
            assert.strictEqual(m.command, 'server_info');
            setImmediate(function() {
              sendServerInfo(m);
            });
            break;
        }
      });
    });

    const server = new Server(new Remote(), 'ws://localhost:5748');

    server.once('connect', function() {
      server.once('response_server_info', function() {
        assert.strictEqual(server.getServerID(), 'ws://localhost:5748 '
          + '(n94pSqypSfddzAVj9qoezHyUoetsrMnwgNuBqRJ3WHvM8aMMf7rW)');
        server.once('disconnect', function() {
          wss.close();
          done();
        });
        server.disconnect();
      });
    });

    server.connect();
  });

  it('Track ledger ranges', function(done) {
    const wss = new ws.Server({
      port: 5748
    });

    wss.once('connection', function(_ws) {
      function sendSubscribe(message) {
        _ws.send(JSON.stringify({
          id: message.id,
          status: 'success',
          type: 'response',
          result: {
            fee_base: 10,
            fee_ref: 10,
            ledger_hash:
            '1838539EE12463C36F2C53B079D807C697E3D93A1936B717E565A4A912E11776',
            ledger_index: 7053695,
            ledger_time: 455414390,
            load_base: 256,
            load_factor: 256,
            random:
            'E56C9154D9BE94D49C581179356C2E084E16D18D74E8B09093F2D61207625E6A',
            reserve_base: 20000000,
            reserve_inc: 5000000,
            server_status: 'full',
            validated_ledgers: '32570-7053695',
            pubkey_node: 'n94pSqypSfddzAVj9qoezHyUoetsrMnwgNuBqRJ3WHvM8aMMf7rW'
          }
        }));
      }

      _ws.on('message', function(message) {
        const m = JSON.parse(message);

        switch (m.command) {
          case 'subscribe':
            assert.strictEqual(m.command, 'subscribe');
            assert.deepEqual(m.streams, ['ledger', 'server']);
            sendSubscribe(m);
            break;
        }
      });
    });

    const server = new Server(new Remote(), 'ws://localhost:5748');

    server.once('connect', function() {
      assert.strictEqual(server.hasLedger(32569), false);
      assert.strictEqual(server.hasLedger(32570), true);
      assert.strictEqual(server.hasLedger(7053695), true);
      assert.strictEqual(server.hasLedger(7053696), false);

      server.emit('message', {
        type: 'ledgerClosed',
        fee_base: 10,
        fee_ref: 10,
        ledger_hash:
          'F29E1F2A2617A88E9DAA14F468B169E6875092ECA0B3B1FA2BE1BC5524DE7CB2',
        ledger_index: 7053696,
        ledger_time: 455327690,
        reserve_base: 20000000,
        reserve_inc: 5000000,
        txn_count: 1
      });

      assert.strictEqual(server.hasLedger(7053696), true);
      assert.strictEqual(server.hasLedger(
        'F29E1F2A2617A88E9DAA14F468B169E6875092ECA0B3B1FA2BE1BC5524DE7CB2'),
        true);

      server.once('disconnect', done);
      wss.close();
    });

    server.connect();
  });
});
