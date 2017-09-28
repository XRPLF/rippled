'use strict';
const assert = require('assert');
const Request = require('ripple-lib').Request;
const Remote = require('ripple-lib').Remote;
const Server = require('ripple-lib').Server;
const Currency = require('ripple-lib').Currency;
const RippleError = require('ripple-lib').RippleError;

function makeServer(url) {
  const server = new Server(new process.EventEmitter(), url);
  server._connected = true;
  return server;
}

const SERVER_INFO = {
  'info': {
    'build_version': '0.25.2-rc1',
    'complete_ledgers': '32570-7016339',
    'hostid': 'LIED',
    'io_latency_ms': 1,
    'last_close': {
      'converge_time_s': 2.013,
      'proposers': 5
    },
    'load_factor': 1,
    'peers': 42,
    'pubkey_node': 'n9LpxYuMx4Epz4Wz8Kg2kH3eBTx1mUtHnYwtCdLoj3HC85L2pvBm',
    'server_state': 'full',
    'validated_ledger': {
      'age': 0,
      'base_fee_xrp': 0.00001,
      'hash':
        'E43FD49087B18031721D9C3C4743FE1692C326AFF7084A2C01B355CE65A4C699',
      'reserve_base_xrp': 20,
      'reserve_inc_xrp': 5,
      'seq': 7016339
    },
    'validation_quorum': 3
  }
};

describe('Request', function() {
  it('Send request', function(done) {
    const remote = {
      request: function(req) {
        assert(req instanceof Request);
        assert.strictEqual(typeof req.message, 'object');
        assert.strictEqual(req.message.command, 'server_info');
        done();
      }
    };

    const request = new Request(remote, 'server_info');

    request.request();

    // Should only request once
    request.request();
  });

  it('Send request -- filterRequest', function(done) {
    const servers = [
      makeServer('wss://localhost:5006'),
      makeServer('wss://localhost:5007')
    ];

    let requests = 0;

    const successResponse = {
      account_data: {
        Account: 'rnoFoLJmqmXe7a7iswk19yfdMHQkbQNrKC',
        Balance: '13188802787',
        Flags: 0,
        LedgerEntryType: 'AccountRoot',
        OwnerCount: 17,
        PreviousTxnID:
          'C6A2313CD9E34FFA3EB42F82B2B30F7FE12A045F1F4FDDAF006B25D7286536DD',
        PreviousTxnLgrSeq: 8828020,
        Sequence: 1406,
        index:
          '4F83A2CF7E70F77F79A307E6A472BFC2585B806A70833CCD1C26105BAE0D6E05'
      },
      ledger_current_index: 9022821,
      validated: false
    };
    const errorResponse = {
      error: 'remoteError',
      error_message: 'Remote reported an error.',
      remote: {
        id: 3,
        status: 'error',
        type: 'response',
        account: 'rnoFoLJmqmXe7a7iswk19yfdMHQkbQNrKC',
        error: 'actNotFound',
        error_code: 15,
        error_message: 'Account not found.',
        ledger_current_index: 9022856,
        request: {
          account: 'rnoFoLJmqmXe7a7iswk19yfdMHQkbQNrKC',
          command: 'account_info',
          id: 3
        },
        validated: false
      }
    };

    function checkRequest(req) {
      assert(req instanceof Request);
      assert.strictEqual(typeof req.message, 'object');
      assert.strictEqual(req.message.command, 'account_info');
    }

    servers[0]._request = function(req) {
      ++requests;
      checkRequest(req);
      req.emit('error', errorResponse);
    };

    servers[1]._request = function(req) {
      ++requests;
      checkRequest(req);
      setImmediate(function() {
        req.emit('success', successResponse);
      });
    };

    const remote = new Remote();
    remote._connected = true;
    remote._servers = servers;

    const request = new Request(remote, 'account_info');

    request.message.account = 'rnoFoLJmqmXe7a7iswk19yfdMHQkbQNrKC';

    request.filter(function(res) {
      return res
        && typeof res === 'object'
        && !res.hasOwnProperty('error');
    });

    request.callback(function(err, res) {
      assert.ifError(err);
      assert.strictEqual(requests, 2, 'Failed to broadcast');
      assert.deepEqual(res, successResponse);
      done();
    });
  });

  it('Send request -- filterRequest -- no success', function(done) {
    const servers = [
      makeServer('wss://localhost:5006'),
      makeServer('wss://localhost:5007')
    ];

    let requests = 0;

    const errorResponse = {
      error: 'remoteError',
      error_message: 'Remote reported an error.',
      remote: {
        id: 3,
        status: 'error',
        type: 'response',
        account: 'rnoFoLJmqmXe7a7iswk19yfdMHQkbQNrKC',
        error: 'actNotFound',
        error_code: 15,
        error_message: 'Account not found.',
        ledger_current_index: 9022856,
        request: {
          account: 'rnoFoLJmqmXe7a7iswk19yfdMHQkbQNrKC',
          command: 'account_info',
          id: 3
        },
        validated: false
      }
    };

    function checkRequest(req) {
      assert(req instanceof Request);
      assert.strictEqual(typeof req.message, 'object');
      assert.strictEqual(req.message.command, 'account_info');
    }

    function sendError(req) {
      ++requests;
      checkRequest(req);
      req.emit('error', errorResponse);
    }

    servers[0]._request = sendError;
    servers[1]._request = sendError;

    const remote = new Remote();
    remote._connected = true;
    remote._servers = servers;

    const request = new Request(remote, 'account_info');

    request.message.account = 'rnoFoLJmqmXe7a7iswk19yfdMHQkbQNrKC';

    request.filter(function(res) {
      return res
        && typeof res === 'object'
        && !res.hasOwnProperty('error');
    });

    request.callback(function(err) {
      setImmediate(function() {
        assert.strictEqual(requests, 2, 'Failed to broadcast');
        assert.deepEqual(err, new RippleError(errorResponse));
        done();
      });
    });
  });

  it('Send request -- filterRequest -- ledger prefilter', function(done) {
    const servers = [
      makeServer('wss://localhost:5006'),
      makeServer('wss://localhost:5007')
    ];

    let requests = 0;

    const successResponse = {
      account_data: {
        Account: 'rnoFoLJmqmXe7a7iswk19yfdMHQkbQNrKC',
        Balance: '13188802787',
        Flags: 0,
        LedgerEntryType: 'AccountRoot',
        OwnerCount: 17,
        PreviousTxnID:
          'C6A2313CD9E34FFA3EB42F82B2B30F7FE12A045F1F4FDDAF006B25D7286536DD',
        PreviousTxnLgrSeq: 8828020,
        Sequence: 1406,
        index:
          '4F83A2CF7E70F77F79A307E6A472BFC2585B806A70833CCD1C26105BAE0D6E05'
      },
      ledger_current_index: 9022821,
      validated: false
    };

    function checkRequest(req) {
      assert(req instanceof Request);
      assert.strictEqual(typeof req.message, 'object');
      assert.strictEqual(req.message.command, 'account_info');
    }

    servers[0]._request = function() {
      assert(false, 'Should not request; server does not have ledger');
    };

    servers[1]._request = function(req) {
      ++requests;
      checkRequest(req);
      setImmediate(function() {
        req.emit('success', successResponse);
      });
    };

    servers[0]._ledgerRanges.addRange(5, 6);
    servers[1]._ledgerRanges.addRange(1, 4);

    const remote = new Remote();
    remote._connected = true;
    remote._servers = servers;

    const request = new Request(remote, 'account_info');
    request.message.account = 'rnoFoLJmqmXe7a7iswk19yfdMHQkbQNrKC';
    request.selectLedger(4);

    request.filter(function(res) {
      return res
        && typeof res === 'object'
        && !res.hasOwnProperty('error');
    });

    request.callback(function(err, res) {
      assert.ifError(err);
      assert.deepEqual(res, successResponse);
      done();
    });
  });

  it('Send request -- filterRequest -- server reconnects', function(done) {
    const servers = [
      makeServer('wss://localhost:5006'),
      makeServer('wss://localhost:5007')
    ];

    let requests = 0;

    const successResponse = {
      account_data: {
        Account: 'rnoFoLJmqmXe7a7iswk19yfdMHQkbQNrKC',
        Balance: '13188802787',
        Flags: 0,
        LedgerEntryType: 'AccountRoot',
        OwnerCount: 17,
        PreviousTxnID:
          'C6A2313CD9E34FFA3EB42F82B2B30F7FE12A045F1F4FDDAF006B25D7286536DD',
        PreviousTxnLgrSeq: 8828020,
        Sequence: 1406,
        index:
          '4F83A2CF7E70F77F79A307E6A472BFC2585B806A70833CCD1C26105BAE0D6E05'
      },
      ledger_current_index: 9022821,
      validated: false
    };
    const errorResponse = {
      error: 'remoteError',
      error_message: 'Remote reported an error.',
      remote: {
        id: 3,
        status: 'error',
        type: 'response',
        account: 'rnoFoLJmqmXe7a7iswk19yfdMHQkbQNrKC',
        error: 'actNotFound',
        error_code: 15,
        error_message: 'Account not found.',
        ledger_current_index: 9022856,
        request: {
          account: 'rnoFoLJmqmXe7a7iswk19yfdMHQkbQNrKC',
          command: 'account_info',
          id: 3
        },
        validated: false
      }
    };

    function checkRequest(req) {
      assert(req instanceof Request);
      assert.strictEqual(typeof req.message, 'object');
      assert.strictEqual(req.message.command, 'account_info');
    }

    servers[0]._connected = false;
    servers[0]._shouldConnect = true;
    servers[0].removeAllListeners('connect');

    servers[0]._request = function(req) {
      ++requests;
      checkRequest(req);
      req.emit('success', successResponse);
    };
    servers[1]._request = function(req) {
      ++requests;
      checkRequest(req);

      req.emit('error', errorResponse);

      servers[0]._connected = true;
      servers[0].emit('connect');
    };

    const remote = new Remote();
    remote._connected = true;
    remote._servers = servers;

    const request = new Request(remote, 'account_info');

    request.message.account = 'rnoFoLJmqmXe7a7iswk19yfdMHQkbQNrKC';

    request.filter(function(res) {
      return res
        && typeof res === 'object'
        && !res.hasOwnProperty('error');
    });

    request.callback(function(err, res) {
      assert.ifError(err);
      setImmediate(function() {
        assert.strictEqual(requests, 2, 'Failed to broadcast');
        assert.deepEqual(res, successResponse);
        done();
      });
    });
  });

  it('Send request -- filterRequest -- server fails to reconnect',
      function(done) {
    const servers = [
      makeServer('wss://localhost:5006'),
      makeServer('wss://localhost:5007')
    ];

    let requests = 0;

    const successResponse = {
      account_data: {
        Account: 'rnoFoLJmqmXe7a7iswk19yfdMHQkbQNrKC',
        Balance: '13188802787',
        Flags: 0,
        LedgerEntryType: 'AccountRoot',
        OwnerCount: 17,
        PreviousTxnID:
          'C6A2313CD9E34FFA3EB42F82B2B30F7FE12A045F1F4FDDAF006B25D7286536DD',
        PreviousTxnLgrSeq: 8828020,
        Sequence: 1406,
        index:
          '4F83A2CF7E70F77F79A307E6A472BFC2585B806A70833CCD1C26105BAE0D6E05'
      },
      ledger_current_index: 9022821,
      validated: false
    };
    const errorResponse = {
      error: 'remoteError',
      error_message: 'Remote reported an error.',
      remote: {
        id: 3,
        status: 'error',
        type: 'response',
        account: 'rnoFoLJmqmXe7a7iswk19yfdMHQkbQNrKC',
        error: 'actNotFound',
        error_code: 15,
        error_message: 'Account not found.',
        ledger_current_index: 9022856,
        request: {
          account: 'rnoFoLJmqmXe7a7iswk19yfdMHQkbQNrKC',
          command: 'account_info',
          id: 3
        },
        validated: false
      }
    };

    function checkRequest(req) {
      assert(req instanceof Request);
      assert.strictEqual(typeof req.message, 'object');
      assert.strictEqual(req.message.command, 'account_info');
    }

    servers[0]._connected = false;
    servers[0]._shouldConnect = true;
    servers[0].removeAllListeners('connect');

    setTimeout(function() {
      servers[0]._connected = true;
      servers[0].emit('connect');
    }, 20);

    servers[0]._request = function(req) {
      ++requests;
      checkRequest(req);
      req.emit('success', successResponse);
    };
    servers[1]._request = function(req) {
      ++requests;
      checkRequest(req);
      req.emit('error', errorResponse);
    };

    const remote = new Remote();
    remote._connected = true;
    remote._servers = servers;

    const request = new Request(remote, 'account_info');
    request.setReconnectTimeout(10);
    request.message.account = 'rnoFoLJmqmXe7a7iswk19yfdMHQkbQNrKC';

    request.filter(function(res) {
      return res
        && typeof res === 'object'
        && !res.hasOwnProperty('error');
    });

    request.callback(function(err) {
      setTimeout(function() {
        // Wait for the request that would emit 'success' to time out
        assert.deepEqual(err, new RippleError(errorResponse));
        assert.deepEqual(servers[0].listeners('connect'), []);
        done();
      }, 20);
    });
  });

  it('Events API', function(done) {
    const server = makeServer('wss://localhost:5006');

    server._request = function(req) {
      assert(req instanceof Request);
      assert.strictEqual(typeof req.message, 'object');
      assert.strictEqual(req.message.command, 'server_info');
      req.emit('success', SERVER_INFO);
    };

    const remote = new Remote();
    remote._connected = true;
    remote._servers = [server];

    const request = new Request(remote, 'server_info');

    request.once('success', function(res) {
      assert.deepEqual(res, SERVER_INFO);
      done();
    });

    request.request();
  });

  it('Callback API', function(done) {
    const server = makeServer('wss://localhost:5006');

    server._request = function(req) {
      assert(req instanceof Request);
      assert.strictEqual(typeof req.message, 'object');
      assert.strictEqual(req.message.command, 'server_info');
      req.emit('success', SERVER_INFO);
    };

    const remote = new Remote();
    remote._connected = true;
    remote._servers = [server];

    const request = new Request(remote, 'server_info');

    request.callback(function(err, res) {
      assert.ifError(err);
      assert.deepEqual(res, SERVER_INFO);
      done();
    });
  });

  it('Timeout', function(done) {
    const server = makeServer('wss://localhost:5006');
    let successEmitted = false;

    server._request = function(req) {
      assert(req instanceof Request);
      assert.strictEqual(typeof req.message, 'object');
      assert.strictEqual(req.message.command, 'server_info');
      setTimeout(function() {
        successEmitted = true;
        req.emit('success', SERVER_INFO);
      }, 200);
    };

    const remote = new Remote();
    remote._connected = true;
    remote._servers = [server];

    const request = new Request(remote, 'server_info');

    request.timeout(10, function() {
      setTimeout(function() {
        assert(successEmitted);
        done();
      }, 200);
    });

    request.callback(function() {
      assert(false, 'Callback should not be called');
    });
  });

  it('Timeout - satisfied', function(done) {
    const server = makeServer('wss://localhost:5006');

    server._request = function(req) {
      assert(req instanceof Request);
      assert.strictEqual(typeof req.message, 'object');
      assert.strictEqual(req.message.command, 'server_info');
      setTimeout(function() {
        req.emit('success', SERVER_INFO);
      }, 200);
    };

    const remote = new Remote();
    remote._connected = true;
    remote._servers = [server];

    const request = new Request(remote, 'server_info');

    let timedOut = false;

    request.once('timeout', function() {
      timedOut = true;
    });

    request.timeout(1000);

    request.callback(function(err, res) {
      assert(!timedOut);
      assert.ifError(err);
      assert.deepEqual(res, SERVER_INFO);
      done();
    });
  });

  it('Set server', function(done) {
    const servers = [
      makeServer('wss://localhost:5006'),
      makeServer('wss://localhost:5007')
    ];

    servers[1]._request = function(req) {
      assert(req instanceof Request);
      assert.strictEqual(typeof req.message, 'object');
      assert.strictEqual(req.message.command, 'server_info');
      done();
    };

    const remote = new Remote();
    remote._connected = true;
    remote._servers = servers;

    remote.getServer = function() {
      return servers[0];
    };

    const request = new Request(remote, 'server_info');
    request.setServer(servers[1]);

    assert.strictEqual(request.server, servers[1]);

    request.request();
  });

  it('Set server - by URL', function(done) {
    const servers = [
      makeServer('wss://localhost:5006'),
      makeServer('wss://127.0.0.1:5007')
    ];

    servers[1]._request = function(req) {
      assert(req instanceof Request);
      assert.strictEqual(typeof req.message, 'object');
      assert.strictEqual(req.message.command, 'server_info');
      done();
    };

    const remote = new Remote();
    remote._connected = true;
    remote._servers = servers;

    remote.getServer = function() {
      return servers[0];
    };

    const request = new Request(remote, 'server_info');
    request.setServer('wss://127.0.0.1:5007');

    assert.strictEqual(request.server, servers[1]);

    request.request();
  });

  it('Set build path', function() {
    const remote = new Remote();
    remote._connected = true;
    remote.local_signing = false;

    const request = new Request(remote, 'server_info');
    request.buildPath(true);
    assert.strictEqual(request.message.build_path, true);
  });

  it('Remove build path', function() {
    const remote = new Remote();
    remote._connected = true;
    remote.local_signing = false;

    const request = new Request(remote, 'server_info');
    request.buildPath(false);
    assert(!request.message.hasOwnProperty('build_path'));
  });

  it('Set build path with local signing', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');

    assert.throws(function() {
      request.buildPath(true);
    }, Error);
  });

  it('Set ledger hash', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');
    request.ledgerHash(
      'B4FD84A73DBD8F0DA9E320D137176EBFED969691DC0AAC7882B76B595A0841AE');
    assert.strictEqual(request.message.ledger_hash,
      'B4FD84A73DBD8F0DA9E320D137176EBFED969691DC0AAC7882B76B595A0841AE');
  });

  it('Set ledger index', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');
    request.ledgerIndex(7016915);
    assert.strictEqual(request.message.ledger_index, 7016915);
  });

  it('Select cached ledger - index', function() {
    const remote = new Remote();
    remote._connected = true;
    remote._ledger_current_index = 1;
    remote._ledger_hash =
      'B4FD84A73DBD8F0DA9E320D137176EBFED969691DC0AAC7882B76B595A0841AE';

    const request = new Request(remote, 'server_info');
    request.ledgerChoose(true);
    assert.strictEqual(request.message.ledger_index, 1);
  });

  it('Select cached ledger - hash', function() {
    const remote = new Remote();
    remote._connected = true;
    remote._ledger_current_index = 1;
    remote._ledger_hash =
      'B4FD84A73DBD8F0DA9E320D137176EBFED969691DC0AAC7882B76B595A0841AE';

    const request = new Request(remote, 'server_info');
    request.ledgerChoose();
    assert.strictEqual(request.message.ledger_hash,
      'B4FD84A73DBD8F0DA9E320D137176EBFED969691DC0AAC7882B76B595A0841AE');
    assert.strictEqual(request.message.ledger_index, undefined);
  });

  it('Select ledger - identifier', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');
    request.ledgerSelect('validated');
    assert.strictEqual(request.message.ledger_index, 'validated');
    assert.strictEqual(request.message.ledger_hash, undefined);
  });

  it('Select ledger - index', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');
    request.ledgerSelect(7016915);
    assert.strictEqual(request.message.ledger_index, 7016915);
    assert.strictEqual(request.message.ledger_hash, undefined);
  });

  it('Select ledger - index (String)', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');
    request.ledgerSelect('7016915');
    assert.strictEqual(request.message.ledger_index, 7016915);
    assert.strictEqual(request.message.ledger_hash, undefined);
  });

  it('Select ledger - hash', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');
    request.ledgerSelect(
      'B4FD84A73DBD8F0DA9E320D137176EBFED969691DC0AAC7882B76B595A0841AE');
    assert.strictEqual(request.message.ledger_hash,
      'B4FD84A73DBD8F0DA9E320D137176EBFED969691DC0AAC7882B76B595A0841AE');
    assert.strictEqual(request.message.ledger_index, undefined);
  });

  it('Select ledger - undefined', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');
    request.ledgerSelect();
    assert.strictEqual(request.message.ledger_hash, undefined);
    assert.strictEqual(request.message.ledger_index, undefined);
    request.ledgerSelect(null);
    assert.strictEqual(request.message.ledger_hash, undefined);
    assert.strictEqual(request.message.ledger_index, undefined);
    request.ledgerSelect(NaN);
    assert.strictEqual(request.message.ledger_hash, undefined);
    assert.strictEqual(request.message.ledger_index, undefined);
  });

  it('Set account_root', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');
    request.accountRoot('r9cZA1mLK5R5Am25ArfXFmqgNwjZgnfk59');
    assert.strictEqual(request.message.account_root,
      'r9cZA1mLK5R5Am25ArfXFmqgNwjZgnfk59');
  });

  it('Set index', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');
    request.index(1);
    assert.strictEqual(request.message.index, 1);
  });

  it('Set offer ID', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');
    request.offerId('r9cZA1mLK5R5Am25ArfXFmqgNwjZgnfk59', 1337);
    assert.deepEqual(request.message.offer, {
      account: 'r9cZA1mLK5R5Am25ArfXFmqgNwjZgnfk59',
      seq: 1337
    });
  });

  it('Set offer index', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');
    request.offerIndex(1337);
    assert.strictEqual(request.message.offer, 1337);
  });

  it('Set secret', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');
    request.secret('mySecret');
    assert.strictEqual(request.message.secret, 'mySecret');
  });

  it('Set transaction hash', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');
    request.txHash(
      'E08D6E9754025BA2534A78707605E0601F03ACE063687A0CA1BDDACFCD1698C7');
    assert.strictEqual(request.message.tx_hash,
      'E08D6E9754025BA2534A78707605E0601F03ACE063687A0CA1BDDACFCD1698C7');
  });

  it('Set transaction JSON', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');
    const txJson = {
      hash: 'E08D6E9754025BA2534A78707605E0601F03ACE063687A0CA1BDDACFCD1698C7'
    };
    request.txJson(txJson);
    assert.deepEqual(request.message.tx_json, txJson);
  });

  it('Set transaction blob', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');
    request.txBlob('asdf');
    assert.strictEqual(request.message.tx_blob, 'asdf');
  });

  it('Set ripple state', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');
    request.rippleState('r9cZA1mLK5R5Am25ArfXFmqgNwjZgnfk59',
      'r9cZA1mLK5R5Am25ArfXFmqgNwjZgnfk59', 'USD');
    assert.deepEqual(request.message.ripple_state, {
      currency: 'USD',
      accounts: ['r9cZA1mLK5R5Am25ArfXFmqgNwjZgnfk59',
                 'r9cZA1mLK5R5Am25ArfXFmqgNwjZgnfk59']
    });
  });

  it('Set accounts', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');

    request.accounts([
      'rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun',
      'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
    ]);

    assert.deepEqual(request.message.accounts, [
      'rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun',
      'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
    ]);
  });

  it('Set accounts - string', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');

    request.accounts('rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun');

    assert.deepEqual(request.message.accounts, [
      'rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun'
    ]);
  });

  it('Set accounts proposed', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');
    request.accountsProposed([
      'rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun',
      'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
    ]);

    assert.deepEqual(request.message.accounts_proposed, [
      'rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun',
      'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
    ]);
  });

  it('Add account', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');

    request.accounts([
      'rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun'
    ]);

    request.addAccount('rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B');

    assert.deepEqual(request.message.accounts, [
      'rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun',
      'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
    ]);
  });

  it('Add account proposed', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');

    request.accountsProposed([
      'rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun'
    ]);

    request.addAccountProposed('rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B');

    assert.deepEqual(request.message.accounts_proposed, [
      'rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun',
      'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
    ]);
  });

  it('Set books', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');

    const books = [
      {
        'taker_gets': {
          'currency': 'EUR',
          'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
        },
        'taker_pays': {
          'currency': 'USD',
          'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
        }
      }
    ];

    request.books(books);

    assert.deepEqual(request.message.books, [
      {
        'taker_gets': {
          'currency': Currency.from_json('EUR').to_hex(),
          'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
        },
        'taker_pays': {
          'currency': Currency.from_json('USD').to_hex(),
          'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
        },
        'snapshot': true
      }
    ]);
  });

  it('Add book', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');

    request.addBook({
      'taker_gets': {
        'currency': 'CNY',
        'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
      },
      'taker_pays': {
        'currency': 'USD',
        'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
      }
    });

    assert.deepEqual(request.message.books, [
      {
        'taker_gets': {
          'currency': Currency.from_json('CNY').to_hex(),
          'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
        },
        'taker_pays': {
          'currency': Currency.from_json('USD').to_hex(),
          'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
        },
        'snapshot': true
      }
    ]);

    const books = [
      {
        'taker_gets': {
          'currency': 'EUR',
          'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
        },
        'taker_pays': {
          'currency': 'USD',
          'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
        }
      }
    ];

    request.books(books);

    assert.deepEqual(request.message.books, [
      {
        'taker_gets': {
          'currency': '0000000000000000000000004555520000000000', // EUR hex
          'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
        },
        'taker_pays': {
          'currency': '0000000000000000000000005553440000000000', // USD hex
          'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
        },
        'snapshot': true
      }
    ]);
  });

  it('Add book - missing side', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');

    request.message.books = undefined;

    const books = [
      {
        'taker_gets': {
          'currency': 'EUR',
          'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
        }
      }
    ];

    assert.throws(function() {
      request.books(books);
    });
  });

  it('Add book - without snapshot', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');

    request.message.books = undefined;

    const book = {
      'taker_gets': {
        'currency': 'EUR',
        'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
      },
      'taker_pays': {
        'currency': 'USD',
        'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
      },
      'both': true
    };

    request.addBook(book, true);

    assert.deepEqual(request.message.books, [{
      'taker_gets': {
        'currency': Currency.from_json('EUR').to_hex(),
        'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
      },
      'taker_pays': {
        'currency': Currency.from_json('USD').to_hex(),
        'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
      },
      'both': true,
      'snapshot': true
    }]);
  });

  it('Add book -  no snapshot', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'server_info');

    request.message.books = undefined;

    const book = {
      'taker_gets': {
        'currency': 'EUR',
        'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
      },
      'taker_pays': {
        'currency': 'USD',
        'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
      },
      'both': true
    };

    request.addBook(book, false);

    assert.deepEqual(request.message.books, [{
      'taker_gets': {
        'currency': Currency.from_json('EUR').to_hex(),
        'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
      },
      'taker_pays': {
        'currency': Currency.from_json('USD').to_hex(),
        'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
      },
      'both': true
    }]);
  });

  it('Add stream', function() {
    const remote = new Remote();
    remote._connected = true;

    const request = new Request(remote, 'subscribe');

    request.addStream('server', 'ledger');
    request.addStream('transactions', 'transactions_proposed');
    request.addStream('accounts', ['rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B']);
    request.addStream('accounts_proposed', [
      'r9cZA1mLK5R5Am25ArfXFmqgNwjZgnfk59'
    ]);
    request.addStream('books', [{
      'taker_gets': {
        'currency': 'EUR',
        'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
      },
      'taker_pays': {
        'currency': 'USD',
        'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
      }
    }]);

    assert.deepEqual(request.message, {
      'command': 'subscribe',
      'id': undefined,
      'streams': [
        'server',
        'ledger',
        'transactions',
        'transactions_proposed',
        'accounts',
        'accounts_proposed',
        'books'
      ],
      'accounts': [
        'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
      ],
      'accounts_proposed': [
        'r9cZA1mLK5R5Am25ArfXFmqgNwjZgnfk59'
      ],
      'books': [
        {
          'taker_gets': {
            'currency': '0000000000000000000000004555520000000000',
            'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
          },
          'taker_pays': {
            'currency': '0000000000000000000000005553440000000000',
            'issuer': 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
          },
          'snapshot': true
        }
      ]
    });
  });
});
