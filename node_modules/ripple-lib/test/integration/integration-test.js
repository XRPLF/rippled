/* eslint-disable max-nested-callbacks */
/* eslint-disable max-params */
'use strict';
const _ = require('lodash');
const assert = require('assert');
const async = require('async');

const errors = require('../../src/api/common/errors');
const composeAsync = require('../../src/api/common/utils').composeAsync;
const common = require('../../src/api/common');
const validate = common.validate;

const wallet = require('./wallet');

const settingsSpecification = require('../fixtures/settings-specification');
const trustlineSpecification = require('../fixtures/trustline-specification');
const payments = require('../fixtures/payments');

const RippleAPI = require('../../src').RippleAPI;


const TIMEOUT = 20000;   // how long before each test case times out
const INTERVAL = 2000;   // how long to wait between checks for validated ledger


function verifyTransaction(testcase, hash, type, options, txData, done) {
  testcase.api.getTransaction(hash, options, (err, data) => {
    if (err instanceof errors.NotFoundError
          && testcase.api.getLedgerVersion() <= options.maxLedgerVersion) {
      console.log('NOT VALIDATED YET...');
      setTimeout(_.partial(verifyTransaction, testcase, hash, type,
        options, txData, done), INTERVAL);
      return;
    } else if (err) {
      done(err);
      return;
    }
    assert(data && data.outcome);
    assert.strictEqual(data.type, type);
    assert.strictEqual(data.address, wallet.getAddress());
    assert.strictEqual(data.outcome.result, 'tesSUCCESS');
    testcase.transactions.push(hash);
    done(null, txData);
  });
}

function testTransaction(testcase, type, lastClosedLedgerVersion,
  txData, done) {

  const signedData = testcase.api.sign(txData, wallet.getSecret());
  console.log('PREPARED...');
  testcase.api.submit(signedData.signedTransaction, (error, data) => {
    console.log('SUBMITTED...');
    if (error) {
      done(error);
      return;
    }
    assert.strictEqual(data.engine_result, 'tesSUCCESS');
    const options = {
      minLedgerVersion: lastClosedLedgerVersion,
      maxLedgerVersion: txData.LastLedgerSequence
    };
    setTimeout(_.partial(verifyTransaction, testcase, signedData.id, type,
      options, txData, done), INTERVAL);
  });
}

function verifyResult(transactionType, transaction, done) {
  assert(transaction);
  assert.strictEqual(transaction.Account, wallet.getAddress());
  assert.strictEqual(transaction.TransactionType, transactionType);
  done(null, transaction);
}


function setup(done) {
  this.api = new RippleAPI({servers: ['wss://s1.ripple.com:443']});
  this.api.connect(() => {
    this.api.remote.getServer().once('ledger_closed', () => {
      // this will initialiaze startLedgerVersion with
      // on first run and will not overwrite on next invocations
      if (!this.startLedgerVersion) {
        this.startLedgerVersion = this.api.getLedgerVersion();
      }
      done();
    });
  });
}

function teardown(done) {
  this.api.remote.disconnect(done);
}

describe.skip('integration tests', function() {
  const instructions = {maxLedgerVersionOffset: 100};
  this.timeout(TIMEOUT);

  before(function() {
    this.transactions = [];
  });
  beforeEach(setup);
  afterEach(teardown);

  it('settings', function(done) {
    const lastClosedLedgerVersion = this.api.getLedgerVersion();
    async.waterfall([
      this.api.prepareSettings.bind(this.api, wallet.getAddress(),
        settingsSpecification, instructions),
      _.partial(verifyResult, 'AccountSet'),
      _.partial(testTransaction, this, 'settings', lastClosedLedgerVersion)
    ], () => done());
  });


  it('trustline', function(done) {
    const lastClosedLedgerVersion = this.api.getLedgerVersion();
    async.waterfall([
      this.api.prepareTrustline.bind(this.api, wallet.getAddress(),
        trustlineSpecification, instructions),
      _.partial(verifyResult, 'TrustSet'),
      _.partial(testTransaction, this, 'trustline', lastClosedLedgerVersion)
    ], () => done());
  });


  it('payment', function(done) {
    const paymentSpecification = payments.payment({
      value: '0.000001',
      sourceAccount: wallet.getAddress(),
      destinationAccount: 'rKmBGxocj9Abgy25J51Mk1iqFzW9aVF9Tc'
    });
    const lastClosedLedgerVersion = this.api.getLedgerVersion();
    async.waterfall([
      this.api.preparePayment.bind(this.api, wallet.getAddress(),
        paymentSpecification, instructions),
      _.partial(verifyResult, 'Payment'),
      _.partial(testTransaction, this, 'payment', lastClosedLedgerVersion)
    ], () => done());
  });


  it('order', function(done) {
    const orderSpecification = {
      direction: 'buy',
      quantity: {
        currency: 'USD',
        value: '237',
        counterparty: 'rMwjYedjc7qqtKYVLiAccJSmCwih4LnE2q'
      },
      totalPrice: {
        currency: 'XRP',
        value: '0.0002'
      }
    };
    const self = this;
    const lastClosedLedgerVersion = this.api.getLedgerVersion();
    async.waterfall([
      this.api.prepareOrder.bind(this.api, wallet.getAddress(),
        orderSpecification, instructions),
      _.partial(verifyResult, 'OfferCreate'),
      _.partial(testTransaction, this, 'order', lastClosedLedgerVersion),
      (txData, callback) => {
        this.api.getOrders(wallet.getAddress(), {}, composeAsync(orders => {
          assert(orders && orders.length > 0);
          const createdOrder = _.first(_.filter(orders, (order) => {
            return order.properties.sequence === txData.Sequence;
          }));
          assert(createdOrder);
          assert.strictEqual(createdOrder.properties.maker,
            wallet.getAddress());
          assert.deepEqual(createdOrder.specification, orderSpecification);
          return txData;
        }, callback));
      },
      (txData, callback) => {
        self.api.prepareOrderCancellation(wallet.getAddress(), txData.Sequence,
          instructions, callback);
      },
      _.partial(verifyResult, 'OfferCancel'),
      _.partial(testTransaction, this, 'orderCancellation',
        lastClosedLedgerVersion)
    ], () => done());
  });


  it('isConnected', function() {
    assert(this.api.isConnected());
  });


  it('getServerInfo', function(done) {
    this.api.getServerInfo(composeAsync(data => {
      assert(data && data.info && data.info.pubkey_node);
    }, done));
  });


  it('getFee', function() {
    const fee = this.api.getFee();
    assert.strictEqual(typeof fee, 'string');
    assert(!isNaN(+fee));
  });


  it('getLedgerVersion', function() {
    const ledgerVersion = this.api.getLedgerVersion();
    assert.strictEqual(typeof ledgerVersion, 'number');
    assert(ledgerVersion >= this.startLedgerVersion);
  });


  it('getTransactions', function(done) {
    const options = {
      outgoing: true,
      minLedgerVersion: this.startLedgerVersion
    };
    this.api.getTransactions(wallet.getAddress(), options,
      composeAsync(transactionsData => {
        assert(transactionsData);
        assert.strictEqual(transactionsData.length, this.transactions.length);
      }, done));
  });


  it('getTrustlines', function(done) {
    const options = {
      currency: trustlineSpecification.currency,
      counterparty: trustlineSpecification.counterparty
    };
    this.api.getTrustlines(wallet.getAddress(), options, composeAsync(data => {
      assert(data && data.length > 0 && data[0] && data[0].specification);
      const specification = data[0].specification;
      assert.strictEqual(specification.limit, trustlineSpecification.limit);
      assert.strictEqual(specification.currency,
        trustlineSpecification.currency);
      assert.strictEqual(specification.counterparty,
        trustlineSpecification.counterparty);
    }, done));
  });


  it('getBalances', function(done) {
    const options = {
      currency: trustlineSpecification.currency,
      counterparty: trustlineSpecification.counterparty
    };
    this.api.getBalances(wallet.getAddress(), options, composeAsync(data => {
      assert(data && data.length > 1 && data[0] && data[1]);
      assert.strictEqual(data[0].currency, 'XRP');
      assert.strictEqual(data[1].currency, trustlineSpecification.currency);
      assert.strictEqual(data[1].counterparty,
        trustlineSpecification.counterparty);
    }, done));
  });


  it('getSettings', function(done) {
    this.api.getSettings(wallet.getAddress(), {}, composeAsync(data => {
      assert(data && data.sequence);
      assert.strictEqual(data.domain, settingsSpecification.domain);
    }, done));
  });


  it('getOrderbook', function(done) {
    const orderbook = {
      base: {
        currency: 'XRP'
      },
      counter: {
        currency: 'USD',
        counterparty: 'rMwjYedjc7qqtKYVLiAccJSmCwih4LnE2q'
      }
    };
    this.api.getOrderbook(wallet.getAddress(), orderbook, {},
      composeAsync(book => {
        assert(book && book.bids && book.bids.length > 0);
        assert(book.asks && book.asks.length > 0);
        const bid = book.bids[0];
        assert(bid && bid.specification && bid.specification.quantity);
        assert(bid.specification.totalPrice);
        assert.strictEqual(bid.specification.direction, 'buy');
        assert.strictEqual(bid.specification.quantity.currency, 'XRP');
        assert.strictEqual(bid.specification.totalPrice.currency, 'USD');
        const ask = book.asks[0];
        assert(ask && ask.specification && ask.specification.quantity);
        assert(ask.specification.totalPrice);
        assert.strictEqual(ask.specification.direction, 'sell');
        assert.strictEqual(ask.specification.quantity.currency, 'XRP');
        assert.strictEqual(ask.specification.totalPrice.currency, 'USD');
      }, done));
  });


  it('getPaths', function(done) {
    const pathfind = {
      source: {
        address: wallet.getAddress()
      },
      destination: {
        address: wallet.getAddress(),
        amount: {
          value: '0.000001',
          currency: trustlineSpecification.currency,
          counterparty: trustlineSpecification.counterparty
        }
      }
    };
    this.api.getPaths(pathfind, composeAsync(data => {
      assert(data && data.length > 0);
      const path = data[0];
      assert(path && path.source);
      assert.strictEqual(path.source.address, wallet.getAddress());
      assert(path.paths && path.paths.length > 0);
    }, done));
  });


  it('generateWallet', function() {
    const newWallet = this.api.generateWallet();
    assert(newWallet && newWallet.address && newWallet.secret);
    validate.addressAndSecret(newWallet);
  });

  /*
  // the 'order' test case already tests order cancellation
  // this is just for cancelling orders if something goes wrong during testing
  it('cancel order', function(done) {
    const sequence = 280;
    const lastClosedLedgerVersion = this.api.getLedgerVersion();
    this.api.prepareOrderCancellation(wallet.getAddress(), sequence,
      instructions, (cerr, cancellationTxData) => {
      if (cerr) {
        done(cerr);
        return;
      }
      verifyResult('OfferCancel', cancellationTxData);
      testTransaction(this, cancellationTxData, 'orderCancellation',
        lastClosedLedgerVersion, done);
    });
  });
  */

});
