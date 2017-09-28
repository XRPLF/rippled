/* eslint-disable max-nested-callbacks */
'use strict';
const _ = require('lodash');
const assert = require('assert-diff');
const path = require('path');
const setupAPI = require('./setup-api');
const RippleAPI = require('ripple-api').RippleAPI;
const common = RippleAPI._PRIVATE.common;
const fixtures = require('./fixtures/api');
const requests = fixtures.requests;
const responses = fixtures.responses;
const addresses = require('./fixtures/addresses');
const hashes = require('./fixtures/hashes');
const address = addresses.ACCOUNT;
const validate = common.validate;
const utils = RippleAPI._PRIVATE.ledgerUtils;
const ledgerClosed = require('./fixtures/api/rippled/ledger-close-newer');
const schemaValidator = RippleAPI._PRIVATE.schemaValidator;

const orderbook = {
  base: {
    currency: 'USD',
    counterparty: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
  },
  counter: {
    currency: 'BTC',
    counterparty: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
  }
};

function checkResult(expected, schemaName, response) {
  // console.log(JSON.stringify(response, null, 2));
  assert.deepEqual(response, expected);
  if (schemaName) {
    schemaValidator.schemaValidate(schemaName, response);
  }
  return response;
}

describe('RippleAPI', function() {
  const instructions = {maxLedgerVersionOffset: 100};
  beforeEach(setupAPI.setup);
  afterEach(setupAPI.teardown);

  it('preparePayment', function() {
    const localInstructions = _.defaults({
      maxFee: '0.000012'
    }, instructions);
    return this.api.preparePayment(
      address, requests.preparePayment, localInstructions).then(
      _.partial(checkResult, responses.preparePayment, 'tx'));
  });

  it('preparePayment with all options specified', function() {
    const localInstructions = {
      maxLedgerVersion: this.api.getLedgerVersion() + 100,
      fee: '0.000012'
    };
    return this.api.preparePayment(
      address, requests.preparePaymentAllOptions, localInstructions).then(
      _.partial(checkResult, responses.preparePaymentAllOptions, 'tx'));
  });

  it('preparePayment without counterparty set', function() {
    const localInstructions = _.defaults({sequence: 23}, instructions);
    return this.api.preparePayment(
      address, requests.preparePaymentNoCounterparty, localInstructions).then(
      _.partial(checkResult, responses.preparePaymentNoCounterparty, 'tx'));
  });

  it('prepareOrder - buy order', function() {
    return this.api.prepareOrder(address, requests.prepareOrder, instructions)
      .then(_.partial(checkResult, responses.prepareOrder, 'tx'));
  });

  it('prepareOrder - sell order', function() {
    return this.api.prepareOrder(
      address, requests.prepareOrderSell, instructions).then(
      _.partial(checkResult, responses.prepareOrderSell, 'tx'));
  });

  it('prepareOrderCancellation', function() {
    return this.api.prepareOrderCancellation(address, 23, instructions).then(
      _.partial(checkResult, responses.prepareOrderCancellation, 'tx'));
  });

  it('prepareTrustline - simple', function() {
    return this.api.prepareTrustline(
      address, requests.prepareTrustline.simple, instructions).then(
        _.partial(checkResult, responses.prepareTrustline.simple, 'tx'));
  });

  it('prepareTrustline - complex', function() {
    return this.api.prepareTrustline(
      address, requests.prepareTrustline.complex, instructions).then(
        _.partial(checkResult, responses.prepareTrustline.complex, 'tx'));
  });

  it('prepareSettings', function() {
    return this.api.prepareSettings(
      address, requests.prepareSettings, instructions).then(
      _.partial(checkResult, responses.prepareSettings.flags, 'tx'));
  });

  it('prepareSettings - regularKey', function() {
    const regularKey = {regularKey: 'rAR8rR8sUkBoCZFawhkWzY4Y5YoyuznwD'};
    return this.api.prepareSettings(address, regularKey, instructions).then(
      _.partial(checkResult, responses.prepareSettings.regularKey, 'tx'));
  });

  it('prepareSettings - flag set', function() {
    const settings = {requireDestinationTag: true};
    return this.api.prepareSettings(address, settings, instructions).then(
      _.partial(checkResult, responses.prepareSettings.flagSet, 'tx'));
  });

  it('prepareSettings - flag clear', function() {
    const settings = {requireDestinationTag: false};
    return this.api.prepareSettings(address, settings, instructions).then(
      _.partial(checkResult, responses.prepareSettings.flagClear, 'tx'));
  });

  it('prepareSettings - string field clear', function() {
    const settings = {walletLocator: null};
    return this.api.prepareSettings(address, settings, instructions).then(
      _.partial(checkResult, responses.prepareSettings.fieldClear, 'tx'));
  });

  it('prepareSettings - integer field clear', function() {
    const settings = {walletSize: null};
    return this.api.prepareSettings(address, settings, instructions)
      .then(data => {
        assert(data);
        assert.strictEqual(data.WalletSize, 0);
      });
  });

  it('prepareSettings - set transferRate', function() {
    const settings = {transferRate: 1};
    return this.api.prepareSettings(address, settings, instructions).then(
      _.partial(checkResult, responses.prepareSettings.setTransferRate, 'tx'));
  });

  it('sign', function() {
    const secret = 'shsWGZcmZz6YsWWmcnpfr6fLTdtFV';
    const result = this.api.sign(requests.sign, secret);
    assert.deepEqual(result, responses.sign);
    schemaValidator.schemaValidate('sign', result);
  });

  it('submit', function() {
    return this.api.submit(responses.sign.signedTransaction).then(
      _.partial(checkResult, responses.submit, 'submit'));
  });

  it('getBalances', function() {
    return this.api.getBalances(address).then(
      _.partial(checkResult, responses.getBalances, 'getBalances'));
  });

  it('getTransaction - payment', function() {
    return this.api.getTransaction(hashes.VALID_TRANSACTION_HASH).then(
      _.partial(checkResult, responses.getTransaction.payment,
        'getTransaction'));
  });

  it('getTransaction - settings', function() {
    const hash =
      '4FB3ADF22F3C605E23FAEFAA185F3BD763C4692CAC490D9819D117CD33BFAA1B';
    return this.api.getTransaction(hash).then(
      _.partial(checkResult, responses.getTransaction.settings,
        'getTransaction'));
  });

  it('getTransaction - order', function() {
    const hash =
      '10A6FB4A66EE80BED46AAE4815D7DC43B97E944984CCD5B93BCF3F8538CABC51';
    return this.api.getTransaction(hash).then(
      _.partial(checkResult, responses.getTransaction.order,
      'getTransaction'));
  });

  it('getTransaction - order cancellation', function() {
    const hash =
      '809335DD3B0B333865096217AA2F55A4DF168E0198080B3A090D12D88880FF0E';
    return this.api.getTransaction(hash).then(
      _.partial(checkResult, responses.getTransaction.orderCancellation,
        'getTransaction'));
  });

  it('getTransaction - trustline set', function() {
    const hash =
      '635A0769BD94710A1F6A76CDE65A3BC661B20B798807D1BBBDADCEA26420538D';
    return this.api.getTransaction(hash).then(
      _.partial(checkResult, responses.getTransaction.trustline,
        'getTransaction'));
  });

  it('getTransaction - trustline frozen off', function() {
    const hash =
      'FE72FAD0FA7CA904FB6C633A1666EDF0B9C73B2F5A4555D37EEF2739A78A531B';
    return this.api.getTransaction(hash).then(
      _.partial(checkResult, responses.getTransaction.trustlineFrozenOff,
        'getTransaction'));
  });

  it('getTransaction - trustline no quality', function() {
    const hash =
      'BAF1C678323C37CCB7735550C379287667D8288C30F83148AD3C1CB019FC9002';
    return this.api.getTransaction(hash).then(
      _.partial(checkResult, responses.getTransaction.trustlineNoQuality,
        'getTransaction'));
  });

  it('getTransaction - not validated', function() {
    const hash =
      '4FB3ADF22F3C605E23FAEFAA185F3BD763C4692CAC490D9819D117CD33BFAA10';
    return this.api.getTransaction(hash).then(
      _.partial(checkResult, responses.getTransaction.notValidated,
      'getTransaction'));
  });

  it('getTransaction - tracking on', function() {
    const hash =
      '8925FC8844A1E930E2CC76AD0A15E7665AFCC5425376D548BB1413F484C31B8C';
    return this.api.getTransaction(hash).then(
      _.partial(checkResult, responses.getTransaction.trackingOn,
        'getTransaction'));
  });

  it('getTransaction - tracking off', function() {
    const hash =
      'C8C5E20DFB1BF533D0D81A2ED23F0A3CBD1EF2EE8A902A1D760500473CC9C582';
    return this.api.getTransaction(hash).then(
      _.partial(checkResult, responses.getTransaction.trackingOff,
        'getTransaction'));
  });

  it('getTransaction - set regular key', function() {
    const hash =
      '278E6687C1C60C6873996210A6523564B63F2844FB1019576C157353B1813E60';
    return this.api.getTransaction(hash).then(
      _.partial(checkResult, responses.getTransaction.setRegularKey,
        'getTransaction'));
  });

  it('getTransaction - not found in range', function() {
    const hash =
      '809335DD3B0B333865096217AA2F55A4DF168E0198080B3A090D12D88880FF0E';
    const options = {
      minLedgerVersion: 32570,
      maxLedgerVersion: 32571
    };
    return this.api.getTransaction(hash, options).then(() => {
      assert(false, 'Should throw NotFoundError');
    }).catch(error => {
      assert(error instanceof this.api.errors.NotFoundError);
    });
  });

  it('getTransaction - not found by hash', function() {
    const hash = hashes.NOTFOUND_TRANSACTION_HASH;
    return this.api.getTransaction(hash).then(() => {
      assert(false, 'Should throw NotFoundError');
    }).catch(error => {
      assert(error instanceof this.api.errors.NotFoundError);
    });
  });

  it('getTransaction - missing ledger history', function() {
    const hash = hashes.NOTFOUND_TRANSACTION_HASH;
    // make gaps in history
    this.api.remote.getServer().emit('message', ledgerClosed);
    return this.api.getTransaction(hash).then(() => {
      assert(false, 'Should throw MissingLedgerHistoryError');
    }).catch(error => {
      assert(error instanceof this.api.errors.MissingLedgerHistoryError);
    });
  });

  it('getTransaction - ledger_index not found', function() {
    const hash =
      '4FB3ADF22F3C605E23FAEFAA185F3BD763C4692CAC490D9819D117CD33BFAA11';
    return this.api.getTransaction(hash).then(() => {
      assert(false, 'Should throw NotFoundError');
    }).catch(error => {
      assert(error instanceof this.api.errors.NotFoundError);
      assert(error.message.indexOf('ledger_index') !== -1);
    });
  });

  it('getTransaction - transaction ledger not found', function() {
    const hash =
      '4FB3ADF22F3C605E23FAEFAA185F3BD763C4692CAC490D9819D117CD33BFAA12';
    return this.api.getTransaction(hash).then(() => {
      assert(false, 'Should throw NotFoundError');
    }).catch(error => {
      assert(error instanceof this.api.errors.NotFoundError);
      assert(error.message.indexOf('ledger not found') !== -1);
    });
  });

  it('getTransaction - ledger missing close time', function() {
    const hash =
      '0F7ED9F40742D8A513AE86029462B7A6768325583DF8EE21B7EC663019DD6A04';
    return this.api.getTransaction(hash).then(() => {
      assert(false, 'Should throw ApiError');
    }).catch(error => {
      assert(error instanceof this.api.errors.ApiError);
    });
  });

  it('getTransactions', function() {
    const options = {types: ['payment', 'order'], initiated: true, limit: 2};
    return this.api.getTransactions(address, options).then(
      _.partial(checkResult, responses.getTransactions,
        'getTransactions'));
  });

  it('getTransactions - earliest first', function() {
    const options = {types: ['payment', 'order'], initiated: true, limit: 2,
      earliestFirst: true
    };
    const expected = _.cloneDeep(responses.getTransactions)
      .sort(utils.compareTransactions);
    return this.api.getTransactions(address, options).then(
      _.partial(checkResult, expected, 'getTransactions'));
  });

  it('getTransactions - earliest first with start option', function() {
    const options = {types: ['payment', 'order'], initiated: true, limit: 2,
      start: hashes.VALID_TRANSACTION_HASH,
      earliestFirst: true
    };
    return this.api.getTransactions(address, options).then(data => {
      assert.strictEqual(data.length, 0);
    });
  });

  it('getTransactions - gap', function() {
    const options = {types: ['payment', 'order'], initiated: true, limit: 2,
      maxLedgerVersion: 348858000
    };
    return this.api.getTransactions(address, options).then(() => {
      assert(false, 'Should throw MissingLedgerHistoryError');
    }).catch(error => {
      assert(error instanceof this.api.errors.MissingLedgerHistoryError);
    });
  });

  it('getTransactions - tx not found', function() {
    const options = {types: ['payment', 'order'], initiated: true, limit: 2,
      start: hashes.NOTFOUND_TRANSACTION_HASH,
      counterparty: address
    };
    return this.api.getTransactions(address, options).then(() => {
      assert(false, 'Should throw NotFoundError');
    }).catch(error => {
      assert(error instanceof this.api.errors.NotFoundError);
    });
  });

  it('getTransactions - filters', function() {
    const options = {types: ['payment', 'order'], initiated: true, limit: 10,
      excludeFailures: true,
      counterparty: addresses.ISSUER
    };
    return this.api.getTransactions(address, options).then(data => {
      assert.strictEqual(data.length, 10);
      assert(_.every(data, t => t.type === 'payment' || t.type === 'order'));
      assert(_.every(data, t => t.outcome.result === 'tesSUCCESS'));
    });
  });

  it('getTransactions - filters for incoming', function() {
    const options = {types: ['payment', 'order'], initiated: false, limit: 10,
      excludeFailures: true,
      counterparty: addresses.ISSUER
    };
    return this.api.getTransactions(address, options).then(data => {
      assert.strictEqual(data.length, 10);
      assert(_.every(data, t => t.type === 'payment' || t.type === 'order'));
      assert(_.every(data, t => t.outcome.result === 'tesSUCCESS'));
    });
  });

  // this is the case where core.RippleError just falls
  // through the api to the user
  it('getTransactions - error', function() {
    const options = {types: ['payment', 'order'], initiated: true, limit: 13};
    return this.api.getTransactions(address, options).then(() => {
      assert(false, 'Should throw RippleError');
    }).catch(error => {
      assert(error instanceof this.api.errors.RippleError);
    });
  });

  // TODO: this doesn't test much, just that it doesn't crash
  it('getTransactions with start option', function() {
    const options = {
      start: hashes.VALID_TRANSACTION_HASH,
      earliestFirst: false,
      limit: 2
    };
    return this.api.getTransactions(address, options).then(
      _.partial(checkResult, responses.getTransactions, 'getTransactions'));
  });

  it('getTrustlines', function() {
    const options = {currency: 'USD'};
    return this.api.getTrustlines(address, options).then(
      _.partial(checkResult, responses.getTrustlines, 'getTrustlines'));
  });

  it('generateAddress', function() {
    function random() {
      return _.fill(Array(16), 0);
    }
    assert.deepEqual(this.api.generateAddress({random}),
                     responses.generateAddress);
  });

  it('getSettings', function() {
    return this.api.getSettings(address).then(
      _.partial(checkResult, responses.getSettings, 'getSettings'));
  });

  it('getAccountInfo', function() {
    return this.api.getAccountInfo(address).then(
      _.partial(checkResult, responses.getAccountInfo, 'getAccountInfo'));
  });

  it('getOrders', function() {
    return this.api.getOrders(address).then(
      _.partial(checkResult, responses.getOrders, 'getOrders'));
  });

  it('getOrderbook', function() {
    return this.api.getOrderbook(address, orderbook).then(
      _.partial(checkResult, responses.getOrderbook, 'getOrderbook'));
  });

  it('getOrderbook - sorted so that best deals come first', function() {
    return this.api.getOrderbook(address, orderbook).then(data => {
      const bidRates = data.bids.map(bid => bid.properties.makerExchangeRate);
      const askRates = data.asks.map(ask => ask.properties.makerExchangeRate);
      // makerExchangeRate = quality = takerPays.value/takerGets.value
      // so the best deal for the taker is the lowest makerExchangeRate
      // bids and asks should be sorted so that the best deals come first
      assert.deepEqual(_.sortBy(bidRates, x => Number(x)), bidRates);
      assert.deepEqual(_.sortBy(askRates, x => Number(x)), askRates);
    });
  });

  it('getOrderbook - currency & counterparty are correct', function() {
    return this.api.getOrderbook(address, orderbook).then(data => {
      const orders = _.flatten([data.bids, data.asks]);
      _.forEach(orders, order => {
        const quantity = order.specification.quantity;
        const totalPrice = order.specification.totalPrice;
        const {base, counter} = orderbook;
        assert.strictEqual(quantity.currency, base.currency);
        assert.strictEqual(quantity.counterparty, base.counterparty);
        assert.strictEqual(totalPrice.currency, counter.currency);
        assert.strictEqual(totalPrice.counterparty, counter.counterparty);
      });
    });
  });

  it('getOrderbook - direction is correct for bids and asks', function() {
    return this.api.getOrderbook(address, orderbook).then(data => {
      assert(_.every(data.bids, bid => bid.specification.direction === 'buy'));
      assert(_.every(data.asks, ask => ask.specification.direction === 'sell'));
    });
  });

  it('getServerInfo', function() {
    return this.api.getServerInfo().then(
      _.partial(checkResult, responses.getServerInfo, 'getServerInfo'));
  });

  it('getServerInfo - error', function() {
    this.mockRippled.returnErrorOnServerInfo = true;
    return this.api.getServerInfo().then(() => {
      assert(false, 'Should throw NetworkError');
    }).catch(error => {
      assert(error instanceof this.api.errors.NetworkError);
      assert(error.message.indexOf('too much load') !== -1);
    });
  });

  it('getFee', function() {
    assert.strictEqual(this.api.getFee(), '0.000012');
  });

  it('disconnect & isConnected', function() {
    assert.strictEqual(this.api.isConnected(), true);
    return this.api.disconnect().then(() => {
      assert.strictEqual(this.api.isConnected(), false);
    });
  });

  it('getPaths', function() {
    return this.api.getPaths(requests.getPaths.normal).then(
      _.partial(checkResult, responses.getPaths.XrpToUsd, 'getPaths'));
  });

  // @TODO
  // need decide what to do with currencies/XRP:
  // if add 'XRP' in currencies, then there will be exception in
  // xrpToDrops function (called from toRippledAmount)
  it('getPaths USD 2 USD', function() {
    return this.api.getPaths(requests.getPaths.UsdToUsd).then(
      _.partial(checkResult, responses.getPaths.UsdToUsd, 'getPaths'));
  });

  it('getPaths XRP 2 XRP', function() {
    return this.api.getPaths(requests.getPaths.XrpToXrp).then(
      _.partial(checkResult, responses.getPaths.XrpToXrp, 'getPaths'));
  });

  it('getPaths - XRP 2 XRP - not enough', function() {
    return this.api.getPaths(requests.getPaths.XrpToXrpNotEnough).then(() => {
      assert(false, 'Should throw NotFoundError');
    }).catch(error => {
      assert(error instanceof this.api.errors.NotFoundError);
    });
  });

  it('getPaths - does not accept currency', function() {
    return this.api.getPaths(requests.getPaths.NotAcceptCurrency).then(() => {
      assert(false, 'Should throw NotFoundError');
    }).catch(error => {
      assert(error instanceof this.api.errors.NotFoundError);
    });
  });

  it('getPaths - no paths', function() {
    return this.api.getPaths(requests.getPaths.NoPaths).then(() => {
      assert(false, 'Should throw NotFoundError');
    }).catch(error => {
      assert(error instanceof this.api.errors.NotFoundError);
    });
  });

  it('getPaths - no paths with source currencies', function() {
    const pathfind = requests.getPaths.NoPathsWithCurrencies;
    return this.api.getPaths(pathfind).then(() => {
      assert(false, 'Should throw NotFoundError');
    }).catch(error => {
      assert(error instanceof this.api.errors.NotFoundError);
    });
  });

  it('getPaths - error: srcActNotFound', function() {
    const pathfind = _.assign({}, requests.getPaths.normal,
      {source: {address: addresses.NOTFOUND}});
    return this.api.getPaths(pathfind).catch(error => {
      assert(error instanceof this.api.errors.RippleError);
    });
  });

  it('getLedgerVersion', function() {
    assert.strictEqual(this.api.getLedgerVersion(), 8819951);
  });

  it('getLedger', function() {
    return this.api.getLedger().then(
      _.partial(checkResult, responses.getLedger.header, 'getLedger'));
  });

  it('getLedger - full, then computeLedgerHash', function() {
    const request = {
      includeTransactions: true,
      includeState: true,
      includeAllData: true,
      ledgerVersion: 38129
    };
    return this.api.getLedger(request).then(
      _.partial(checkResult, responses.getLedger.full, 'getLedger'))
      .then(response => {
        const ledger = _.assign({}, response,
          {parentCloseTime: response.closeTime});
        const hash = this.api.computeLedgerHash(ledger);
        assert.strictEqual(hash,
          'E6DB7365949BF9814D76BCC730B01818EB9136A89DB224F3F9F5AAE4569D758E');
      });
  });

  it('ledger utils - compareTransactions', function() {
    let first = {outcome: {ledgerVersion: 1, indexInLedger: 100}};
    let second = {outcome: {ledgerVersion: 1, indexInLedger: 200}};

    assert.strictEqual(utils.compareTransactions(first, second), -1);

    first = {outcome: {ledgerVersion: 1, indexInLedger: 100}};
    second = {outcome: {ledgerVersion: 1, indexInLedger: 100}};

    assert.strictEqual(utils.compareTransactions(first, second), 0);

    first = {outcome: {ledgerVersion: 1, indexInLedger: 200}};
    second = {outcome: {ledgerVersion: 1, indexInLedger: 100}};

    assert.strictEqual(utils.compareTransactions(first, second), 1);
  });

  it('ledger utils - renameCounterpartyToIssuer', function() {
    assert.strictEqual(utils.renameCounterpartyToIssuer(undefined), undefined);
    const amountArg = {issuer: '1'};
    assert.deepEqual(utils.renameCounterpartyToIssuer(amountArg), amountArg);
  });

  it('ledger utils - getRecursive', function(done) {
    function getter(marker, limit, callback) {
      if (marker === undefined) {
        callback(null, {marker: 'A', limit: limit, results: [1]});
      } else {
        callback(new Error(), null);
      }
    }
    utils.getRecursive(getter, 10, (error) => {
      assert(error instanceof Error);
      done();
    });
  });

  describe('schema-validator', function() {
    beforeEach(function() {
      const schema = schemaValidator.loadSchema(path.join(__dirname,
        './fixtures/schemas/ledgerhash.json'));
      schemaValidator.SCHEMAS.ledgerhash = schema;
    });

    it('valid', function() {
      assert.doesNotThrow(function() {
        schemaValidator.schemaValidate('ledgerhash',
          '0F7ED9F40742D8A513AE86029462B7A6768325583DF8EE21B7EC663019DD6A0F');
      });
    });

    it('invalid', function() {
      assert.throws(function() {
        schemaValidator.schemaValidate('ledgerhash', 'invalid');
      }, this.api.errors.ValidationError);
    });

    it('invalid - empty value', function() {
      assert.throws(function() {
        schemaValidator.schemaValidate('ledgerhash', '');
      }, this.api.errors.ValidationError);
    });

    it('load schema error', function() {
      assert.throws(function() {
        schemaValidator.loadSchema('/bad/file/name');
      }, Error);
    });

    it('schema not found error', function() {
      assert.throws(function() {
        schemaValidator.schemaValidate('unexisting', 'anything');
      }, /schema not found/);
    });

  });

  describe('validator', function() {

    it('validateLedgerRange', function() {
      const options = {
        minLedgerVersion: 20000,
        maxLedgerVersion: 10000
      };
      assert.throws(_.partial(validate.getTransactionsOptions, options),
        this.api.errors.ValidationError);
      assert.throws(_.partial(validate.getTransactionsOptions, options),
        /minLedgerVersion must not be greater than maxLedgerVersion/);
    });

    it('addressAndSecret', function() {
      const noSecret = {address: address};
      assert.throws(_.partial(validate.addressAndSecret, noSecret),
        this.api.errors.ValidationError);
      assert.throws(_.partial(validate.addressAndSecret, noSecret),
        /Parameter missing/);
      const badSecret = {address: address, secret: 'sbad'};
      assert.throws(_.partial(validate.addressAndSecret, badSecret),
        this.api.errors.ValidationError);
      const goodWallet = {address: 'rpZMK8hwyrBvLorFNWHRCGt88nCJWbixur',
        secret: 'shzjfakiK79YQdMjy4h8cGGfQSV6u'
      };
      assert.doesNotThrow(_.partial(validate.addressAndSecret, goodWallet));
    });

    it('secret', function() {
      assert.doesNotThrow(_.partial(validate.secret,
        'shzjfakiK79YQdMjy4h8cGGfQSV6u'));
      assert.throws(_.partial(validate.secret, 1),
        /Invalid parameter/);
      assert.throws(_.partial(validate.secret, ''),
        this.api.errors.ValidationError);
      assert.throws(_.partial(validate.secret, 's!!!'),
        this.api.errors.ValidationError);
      assert.throws(_.partial(validate.secret, 'passphrase'),
        this.api.errors.ValidationError);
      // 32 0s is a valid hex repr of seed bytes
      const hex = new Array(33).join('0');
      assert.throws(_.partial(validate.secret, hex),
        this.api.errors.ValidationError);
    });

  });

  describe('common utils', function() {

    it('wrapCatch', function(done) {
      common.wrapCatch(function() {
        throw new Error('error');
      })(function(error) {
        assert(error instanceof Error);
        done();
      });
    });

    it('convertExceptions', function() {
      assert.throws(common.convertExceptions(function() {
        throw new Error('fall through');
      }), this.api.errors.ApiError);
      assert.throws(common.convertExceptions(function() {
        throw new Error('fall through');
      }), /fall through/);
    });

  });

  describe('common errors', function() {

    it('TransactionError', function() {
      // TransactionError is not used anywhere, so just test its creation
      assert.throws(function() {
        throw new common.errors.TransactionError('fall through');
      }, this.api.errors.TransactionError);
      assert.throws(function() {
        throw new common.errors.TransactionError('fall through');
      }, /fall through/);
    });

    it('TimeOutError', function() {
      // TimeOutError is not used anywhere, so just test its creation
      assert.throws(function() {
        throw new common.errors.TimeOutError('fall through');
      }, this.api.errors.TimeOutError);
      assert.throws(function() {
        throw new common.errors.TimeOutError('fall through');
      }, /fall through/);
    });

    it('RippledNetworkError', function() {
      assert.throws(function() {
        throw new common.errors.RippledNetworkError();
      }, /Cannot connect to rippled/);
    });

  });

});

describe('RippleAPI - offline', function() {
  it('prepareSettings and sign', function() {
    const api = new RippleAPI();
    const secret = 'shsWGZcmZz6YsWWmcnpfr6fLTdtFV';
    const settings = requests.prepareSettings;
    const instructions = {
      sequence: 23,
      maxLedgerVersion: 8820051,
      fee: '0.000012'
    };
    return api.prepareSettings(address, settings, instructions).then(txJSON => {
      assert.deepEqual(txJSON, responses.prepareSettings.flags);
      assert.deepEqual(api.sign(txJSON, secret), responses.sign);
    });
  });

  it('computeLedgerHash', function() {
    const api = new RippleAPI();
    const header = requests.computeLedgerHash.header;
    const ledgerHash = api.computeLedgerHash(header);
    assert.strictEqual(ledgerHash,
      'F4D865D83EB88C1A1911B9E90641919A1314F36E1B099F8E95FE3B7C77BE3349');
  });

  it('computeLedgerHash - with transactions', function() {
    const api = new RippleAPI();
    const header = _.omit(requests.computeLedgerHash.header,
      'transactionHash');
    header.rawTransactions = JSON.stringify(
      requests.computeLedgerHash.transactions);
    const ledgerHash = api.computeLedgerHash(header);
    assert.strictEqual(ledgerHash,
      'F4D865D83EB88C1A1911B9E90641919A1314F36E1B099F8E95FE3B7C77BE3349');
  });

  it('computeLedgerHash - incorrent transaction_hash', function() {
    const api = new RippleAPI();
    const header = _.assign({}, requests.computeLedgerHash.header,
      {transactionHash:
        '325EACC5271322539EEEC2D6A5292471EF1B3E72AE7180533EFC3B8F0AD435C9'});
    header.rawTransactions = JSON.stringify(
      requests.computeLedgerHash.transactions);
    assert.throws(() => api.computeLedgerHash(header));
  });

  it('isValidAddress - valid', function() {
    const api = new RippleAPI();
    assert(api.isValidAddress(address));
  });

  it('isValidAddress - invalid', function() {
    const api = new RippleAPI();
    assert(!api.isValidAddress(address.slice(0, -1) + 'a'));
  });

  it('isValidAddress - invalid - hex representation', function() {
    const api = new RippleAPI();
    const hex = '6e3efa86a5eb0a3c5dc9beb3a204783bb00e1913';
    assert(!api.isValidAddress(hex));
  });

/* eslint-disable no-unused-vars */
  it('RippleAPI - implicit server port', function() {
    const api = new RippleAPI({servers: ['wss://s1.ripple.com']});
  });
/* eslint-enable no-unused-vars */
  it('RippleAPI invalid options', function() {
    assert.throws(() => new RippleAPI({invalid: true}));
  });

  it('RippleAPI valid options', function() {
    const api = new RippleAPI({trace: true, servers: ['wss://s:1']});
    assert(api.remote.trace);
    assert.deepEqual(api.remote.servers, ['wss://s:1']);
  });

  it('RippleAPI invalid server uri', function() {
    assert.throws(() => new RippleAPI({servers: ['wss//s:1']}));
  });

});
