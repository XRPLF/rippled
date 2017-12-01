'use strict';
const net = require('net');
const RippleAPI = require('ripple-api').RippleAPI;
const ledgerClosed = require('./fixtures/api/rippled/ledger-close');
const createMockRippled = require('./mock-rippled');

// using a free port instead of a constant port enables parallelization
function getFreePort(callback) {
  const server = net.createServer();
  let port;
  server.on('listening', function() {
    port = server.address().port;
    server.close();
  });
  server.on('close', function() {
    callback(null, port);
  });
  server.on('error', function(error) {
    callback(error);
  });
  server.listen(0);
}

function setupMockRippledConnection(testcase, port, done) {
  testcase.mockRippled = createMockRippled(port);
  testcase.api = new RippleAPI({servers: ['ws://localhost:' + port]});
  testcase.api.connect().then(() => {
    testcase.api.remote.getServer().once('ledger_closed', () => done());
    testcase.api.remote.getServer().emit('message', ledgerClosed);
  }).catch(done);
}

function setup(done) {
  getFreePort((error, port) => {
    if (error) {
      throw new Error('Unable to obtain a free port: ' + error);
    }
    setupMockRippledConnection(this, port, done);
  });
}

function teardown(done) {
  this.api.disconnect().then(() => {
    this.mockRippled.close();
    setImmediate(done);
  }).catch(done);
}

module.exports = {
  setup: setup,
  teardown: teardown
};
