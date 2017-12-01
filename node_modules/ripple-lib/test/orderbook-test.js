/*eslint-disable max-len */

'use strict';

const assert = require('assert-diff');
const Remote = require('ripple-lib').Remote;
const Currency = require('ripple-lib').Currency;
const Amount = require('ripple-lib').Amount;
const Meta = require('ripple-lib').Meta;
const addresses = require('./fixtures/addresses');
const fixtures = require('./fixtures/orderbook');

describe('OrderBook', function() {
  this.timeout(0);

  it('toJSON', function() {
    let book = new Remote().createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    assert.deepEqual(book.toJSON(), {
      taker_gets: {
        currency: Currency.from_json('XRP').to_hex()
      },
      taker_pays: {
        currency: Currency.from_json('BTC').to_hex(),
        issuer: addresses.ISSUER
      }
    });

    book = new Remote().createOrderBook({
      issuer_gets: addresses.ISSUER,
      currency_gets: 'BTC',
      currency_pays: 'XRP'
    });

    assert.deepEqual(book.toJSON(), {
      taker_gets: {
        currency: Currency.from_json('BTC').to_hex(),
        issuer: addresses.ISSUER
      },
      taker_pays: {
        currency: Currency.from_json('XRP').to_hex()
      }
    });
  });

  it('Check orderbook validity', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    assert(book.isValid());
  });

  it('Automatic subscription (based on listeners)', function(done) {
    const book = new Remote().createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    book.subscribe = function() {
      done();
    };

    book.on('model', function() {});
  });

  it('Subscribe', function(done) {
    const book = new Remote().createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    let requestedOffers = false;

    book.subscribeTransactions = function() {
      assert(requestedOffers);
      done();
    };

    book.requestOffers = function(callback) {
      requestedOffers = true;
      callback();
    };

    book.subscribe();
  });

  it('Unsubscribe', function(done) {
    const book = new Remote().createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    book.once('unsubscribe', function() {
      done();
    });

    book.on('model', function() {});

    book.unsubscribe();

    assert(!book._subscribed);
    assert(!book._shouldConnect);
    assert.deepEqual(book.listeners(), []);
  });

  it('Automatic unsubscription - remove all listeners', function(done) {
    const book = new Remote().createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    book.unsubscribe = function() {
      done();
    };

    book.on('model', function() {});
    book.removeAllListeners('model');
  });

  it('Automatic unsubscription - once listener', function(done) {
    const book = new Remote().createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    book.unsubscribe = function() {
      done();
    };

    book.once('model', function() {});
    book.emit('model', {});
  });

  it('Set owner funds', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    book._issuerTransferRate = 1000000000;
    book.setOwnerFunds(addresses.ACCOUNT, '1');

    assert.strictEqual(book.getOwnerFunds(addresses.ACCOUNT).to_text(), '1');
  });

  it('Set owner funds - unadjusted funds', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    book._issuerTransferRate = 1002000000;
    book.setOwnerFunds(addresses.ACCOUNT, '1');

    assert.strictEqual(book._ownerFundsUnadjusted[addresses.ACCOUNT], '1');
  });

  it('Set owner funds - invalid account', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    assert.throws(function() {
      book.setOwnerFunds('0rrrrrrrrrrrrrrrrrrrrBZbvji', '1');
    });
  });

  it('Set owner funds - invalid amount', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    assert.throws(function() {
      book.setOwnerFunds(addresses.ACCOUNT, null);
    });
  });

  it('Has owner funds', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    book._ownerFunds[addresses.ACCOUNT] = '1';
    assert(book.hasOwnerFunds(addresses.ACCOUNT));
  });

  it('Delete owner funds', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    book._ownerFunds[addresses.ACCOUNT] = '1';
    assert(book.hasOwnerFunds(addresses.ACCOUNT));

    book.deleteOwnerFunds(addresses.ACCOUNT);
    assert(!book.hasOwnerFunds(addresses.ACCOUNT));
  });

  it('Delete owner funds', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'BTC',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._ownerFunds[addresses.ACCOUNT] = '1';
    assert(book.hasOwnerFunds(addresses.ACCOUNT));

    assert.throws(function() {
      book.deleteOwnerFunds('0rrrrrrrrrrrrrrrrrrrrBZbvji');
    });
  });

  it('Increment owner offer count', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'BTC',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    assert.strictEqual(book.incrementOwnerOfferCount(addresses.ACCOUNT), 1);
    assert.strictEqual(book.getOwnerOfferCount(addresses.ACCOUNT), 1);
  });

  it('Increment owner offer count - invalid address', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'BTC',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    assert.throws(function() {
      book.incrementOwnerOfferCount('zrrrrrrrrrrrrrrrrrrrBZbvji');
    });
  });

  it('Decrement owner offer count', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'BTC',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book.incrementOwnerOfferCount(addresses.ACCOUNT);

    assert.strictEqual(book.decrementOwnerOfferCount(addresses.ACCOUNT), 0);
    assert.strictEqual(book.getOwnerOfferCount(addresses.ACCOUNT), 0);
  });

  it('Decrement owner offer count - no more offers', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'BTC',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book.incrementOwnerOfferCount(addresses.ACCOUNT);

    assert.strictEqual(book.decrementOwnerOfferCount(addresses.ACCOUNT), 0);
    assert.strictEqual(book.getOwnerOfferCount(addresses.ACCOUNT), 0);
    assert.strictEqual(book.getOwnerFunds(addresses.ACCOUNT), undefined);
  });

  it('Decrement owner offer count - invalid address', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'BTC',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    assert.throws(function() {
      book.decrementOwnerOfferCount('zrrrrrrrrrrrrrrrrrrrBZbvji');
    });
  });

  it('Subtract owner offer total', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'BTC',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._ownerOffersTotal[addresses.ACCOUNT] = Amount.from_json({
      value: 3,
      currency: 'BTC',
      issuer: addresses.ISSUER
    });

    const newAmount = book.subtractOwnerOfferTotal(addresses.ACCOUNT, {
      value: 2,
      currency: 'BTC',
      issuer: addresses.ISSUER
    });

    const offerTotal = Amount.from_json({
      value: 1,
      currency: 'BTC',
      issuer: addresses.ISSUER
    });

    assert(newAmount.equals(offerTotal));
  });

  it('Subtract owner offer total - negative total', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'BTC',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    assert.throws(function() {
      book.subtractOwnerOfferTotal(addresses.ACCOUNT, {
        value: 2,
        currency: 'BTC',
        issuer: addresses.ISSUER
      });
    });
  });

  it('Get owner offer total', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'BTC',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });
    book._ownerOffersTotal[addresses.ACCOUNT] = Amount.from_json({
      value: 3,
      currency: 'BTC',
      issuer: addresses.ISSUER
    });

    assert.strictEqual(book.getOwnerOfferTotal(addresses.ACCOUNT).to_text(), '3');
  });

  it('Get owner offer total - native', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._ownerOffersTotal[addresses.ACCOUNT] = Amount.from_json('3');

    assert.strictEqual(book.getOwnerOfferTotal(addresses.ACCOUNT).to_text(), '3');
  });

  it('Get owner offer total - no total', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'BTC',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    assert.strictEqual(book.getOwnerOfferTotal(addresses.ACCOUNT).to_text(), '0');
  });

  it('Get owner offer total - native - no total', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    assert(book.getOwnerOfferTotal(addresses.ACCOUNT).to_text(), '0');
  });

  it('Apply transfer rate - cached transfer rate', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'BTC',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._issuerTransferRate = 1002000000;

    assert.strictEqual(book.applyTransferRate('1'), '0.9980039920159681');
  });

  it('Apply transfer rate - native currency', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    book._issuerTransferRate = 1000000000;

    assert.strictEqual(book.applyTransferRate('0.9980039920159681'), '0.9980039920159681');
  });

  it('Apply transfer rate - invalid balance', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'BTC',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    assert.throws(function() {
      book.applyTransferRate('asdf');
    });
  });

  it('Apply transfer rate - invalid transfer rate', function() {
    const book = new Remote().createOrderBook({
      currency_gets: 'BTC',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    assert.throws(function() {
      book.applyTransferRate('1');
    });
  });

  it('Request transfer rate', function() {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'BTC',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    remote.request = function(request) {
      assert.deepEqual(request.message, {
        command: 'account_info',
        id: undefined,
        account: addresses.ISSUER
      });

      request.emit('success', {
        account_data: {
          TransferRate: 1002000000
        }
      });
    };

    book.requestTransferRate(function(err, rate) {
      assert.ifError(err);
      assert.strictEqual(rate, 1002000000);
    });
  });

  it('Request transfer rate - not set', function() {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'BTC',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    remote.request = function(request) {
      assert.deepEqual(request.message, {
        command: 'account_info',
        id: undefined,
        account: addresses.ISSUER
      });

      request.emit('success', {
        account_data: {
        }
      });
    };

    book.requestTransferRate(function(err, rate) {
      assert.ifError(err);
      assert.strictEqual(rate, 1000000000);
    });
  });

  it('Request transfer rate - cached transfer rate', function() {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'BTC',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._issuerTransferRate = 1002000000;

    remote.request = function() {
      assert(false);
    };

    book.requestTransferRate(function(err, rate) {
      assert.ifError(err);
      assert.strictEqual(rate, 1002000000);
    });
  });

  it('Request transfer rate - native currency', function() {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    remote.request = function() {
      assert(false);
    };

    book.requestTransferRate(function(err, rate) {
      assert.ifError(err);
      assert.strictEqual(rate, 1000000000);
      assert.strictEqual(book._issuerTransferRate, 1000000000);
    });
  });

  it('Set offer funded amount - iou/xrp - fully funded', function() {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'BTC',
      currency_pays: 'XRP',
      issuer_gets: addresses.ISSUER
    });

    book._issuerTransferRate = 1000000000;

    const offer = {
      Account: addresses.ACCOUNT,
      TakerGets: {
        value: '100',
        currency: 'BTC',
        issuer: addresses.ISSUER
      },
      TakerPays: '123456'
    };

    book.setOwnerFunds(addresses.ACCOUNT, '100.1234');
    book.setOfferFundedAmount(offer);

    const expected = {
      Account: addresses.ACCOUNT,
      TakerGets: offer.TakerGets,
      TakerPays: offer.TakerPays,
      is_fully_funded: true,
      taker_gets_funded: '100',
      taker_pays_funded: '123456',
      owner_funds: '100.1234'
    };

    assert.deepEqual(offer, expected);
  });

  it('Set offer funded amount - iou/xrp - unfunded', function() {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'BTC',
      currency_pays: 'XRP',
      issuer_gets: addresses.ISSUER
    });

    book._issuerTransferRate = 1000000000;

    const offer = {
      Account: addresses.ACCOUNT,
      TakerGets: {
        value: '100',
        currency: 'BTC',
        issuer: addresses.ISSUER
      },
      TakerPays: '123456',
      quality: '1234.56'
    };

    book.setOwnerFunds(addresses.ACCOUNT, '99');
    book.setOfferFundedAmount(offer);

    const expected = {
      Account: addresses.ACCOUNT,
      TakerGets: offer.TakerGets,
      TakerPays: offer.TakerPays,
      is_fully_funded: false,
      taker_gets_funded: '99',
      taker_pays_funded: '122221',
      owner_funds: '99',
      quality: '1234.56'
    };

    assert.deepEqual(offer, expected);
  });

  it('Set offer funded amount - xrp/iou - funded', function() {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    book._issuerTransferRate = 1000000000;

    const offer = {
      Account: addresses.ACCOUNT,
      TakerGets: '100',
      TakerPays: {
        value: '123.456',
        currency: 'BTC',
        issuer: addresses.ISSUER
      }
    };

    book.setOwnerFunds(addresses.ACCOUNT, '100100000');
    book.setOfferFundedAmount(offer);

    const expected = {
      Account: addresses.ACCOUNT,
      TakerGets: offer.TakerGets,
      TakerPays: offer.TakerPays,
      is_fully_funded: true,
      taker_gets_funded: '100',
      taker_pays_funded: '123.456',
      owner_funds: '100100000'
    };

    assert.deepEqual(offer, expected);
  });

  it('Set offer funded amount - xrp/iou - unfunded', function() {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    book._issuerTransferRate = 1000000000;

    const offer = {
      Account: addresses.ACCOUNT,
      TakerGets: '100',
      TakerPays: {
        value: '123.456',
        currency: 'BTC',
        issuer: addresses.ISSUER
      },
      quality: '1.23456'
    };

    book.setOwnerFunds(addresses.ACCOUNT, '99');
    book.setOfferFundedAmount(offer);

    const expected = {
      Account: addresses.ACCOUNT,
      TakerGets: offer.TakerGets,
      TakerPays: offer.TakerPays,
      is_fully_funded: false,
      taker_gets_funded: '99',
      taker_pays_funded: '122.22144',
      owner_funds: '99',
      quality: '1.23456'
    };

    assert.deepEqual(offer, expected);
  });

  it('Set offer funded amount - zero funds', function() {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    book._issuerTransferRate = 1000000000;

    const offer = {
      Account: addresses.ACCOUNT,
      TakerGets: {
        value: '100',
        currency: 'BTC',
        issuer: addresses.ISSUER
      },
      TakerPays: '123456'
    };

    book.setOwnerFunds(addresses.ACCOUNT, '0');
    book.setOfferFundedAmount(offer);

    assert.deepEqual(offer, {
      Account: addresses.ACCOUNT,
      TakerGets: offer.TakerGets,
      TakerPays: offer.TakerPays,
      is_fully_funded: false,
      taker_gets_funded: '0',
      taker_pays_funded: '0',
      owner_funds: '0'
    });
  });

  it('Check is balance change node', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    const meta = new Meta({
      AffectedNodes: [{
        ModifiedNode: {
          FinalFields: {
            Balance: {
              currency: 'USD',
              issuer: addresses.ISSUER,
              value: '-1'
            },
            Flags: 131072,
            HighLimit: {
              currency: 'USD',
              issuer: addresses.ISSUER,
              value: '100'
            },
            HighNode: '0000000000000000',
            LowLimit: {
              currency: 'USD',
              issuer: 'r3PDtZSa5LiYp1Ysn1vMuMzB59RzV3W9QH',
              value: '0'
            },
            LowNode: '0000000000000000'
          },
          LedgerEntryType: 'RippleState',
          LedgerIndex: 'EA4BF03B4700123CDFFB6EB09DC1D6E28D5CEB7F680FB00FC24BC1C3BB2DB959',
          PreviousFields: {
            Balance: {
              currency: 'USD',
              issuer: addresses.ISSUER,
              value: '0'
            }
          },
          PreviousTxnID: '53354D84BAE8FDFC3F4DA879D984D24B929E7FEB9100D2AD9EFCD2E126BCCDC8',
          PreviousTxnLgrSeq: 343570
        }
      }]
    });

    assert(book.isBalanceChangeNode(meta.getNodes()[0]));
  });

  it('Check is balance change node - not balance change', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    const meta = new Meta({
      AffectedNodes: [{
        ModifiedNode: {
          FinalFields: {
            Balance: {
              currency: 'USD',
              issuer: addresses.ISSUER,
              value: '-1'
            },
            Flags: 131072,
            HighLimit: {
              currency: 'USD',
              issuer: 'r9cZA1mLK5R5Am25ArfXFmqgNwjZgnfk59',
              value: '100'
            },
            HighNode: '0000000000000000',
            LowLimit: {
              currency: 'USD',
              issuer: 'r3PDtZSa5LiYp1Ysn1vMuMzB59RzV3W9QH',
              value: '0'
            },
            LowNode: '0000000000000000'
          },
          LedgerEntryType: 'RippleState',
          LedgerIndex: 'EA4BF03B4700123CDFFB6EB09DC1D6E28D5CEB7F680FB00FC24BC1C3BB2DB959',
          PreviousTxnID: '53354D84BAE8FDFC3F4DA879D984D24B929E7FEB9100D2AD9EFCD2E126BCCDC8',
          PreviousTxnLgrSeq: 343570
        }
      }]
    });

    assert(!book.isBalanceChangeNode(meta.getNodes()[0]));
  });

  it('Check is balance change node - different currency', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'BTC',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    const meta = new Meta({
      AffectedNodes: [{
        ModifiedNode: {
          FinalFields: {
            Balance: {
              currency: 'USD',
              issuer: addresses.ISSUER,
              value: '-1'
            },
            Flags: 131072,
            HighLimit: {
              currency: 'USD',
              issuer: addresses.ISSUER,
              value: '100'
            },
            HighNode: '0000000000000000',
            LowLimit: {
              currency: 'USD',
              issuer: 'r3PDtZSa5LiYp1Ysn1vMuMzB59RzV3W9QH',
              value: '0'
            },
            LowNode: '0000000000000000'
          },
          LedgerEntryType: 'RippleState',
          LedgerIndex: 'EA4BF03B4700123CDFFB6EB09DC1D6E28D5CEB7F680FB00FC24BC1C3BB2DB959',
          PreviousFields: {
            Balance: {
              currency: 'USD',
              issuer: addresses.ISSUER,
              value: '0'
            }
          },
          PreviousTxnID: '53354D84BAE8FDFC3F4DA879D984D24B929E7FEB9100D2AD9EFCD2E126BCCDC8',
          PreviousTxnLgrSeq: 343570
        }
      }]
    });

    assert(!book.isBalanceChangeNode(meta.getNodes()[0]));
  });

  it('Check is balance change node - different issuer', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    const meta = new Meta({
      AffectedNodes: [{
        ModifiedNode: {
          FinalFields: {
            Balance: {
              currency: 'USD',
              issuer: addresses.ISSUER,
              value: '-1'
            },
            Flags: 131072,
            HighLimit: {
              currency: 'USD',
              issuer: 'r9cZA1mLK5R5Am25ArfXFmqgNwjZgnfk59',
              value: '100'
            },
            HighNode: '0000000000000000',
            LowLimit: {
              currency: 'USD',
              issuer: 'r3PDtZSa5LiYp1Ysn1vMuMzB59RzV3W9QH',
              value: '0'
            },
            LowNode: '0000000000000000'
          },
          LedgerEntryType: 'RippleState',
          LedgerIndex: 'EA4BF03B4700123CDFFB6EB09DC1D6E28D5CEB7F680FB00FC24BC1C3BB2DB959',
          PreviousFields: {
            Balance: {
              currency: 'USD',
              issuer: addresses.ISSUER,
              value: '0'
            }
          },
          PreviousTxnID: '53354D84BAE8FDFC3F4DA879D984D24B929E7FEB9100D2AD9EFCD2E126BCCDC8',
          PreviousTxnLgrSeq: 343570
        }
      }]
    });

    assert(!book.isBalanceChangeNode(meta.getNodes()[0]));
  });

  it('Check is balance change node - native currency', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    const meta = new Meta({
      AffectedNodes: [{
        ModifiedNode: {
          FinalFields: {
            Account: 'r9cZA1mLK5R5Am25ArfXFmqgNwjZgnfk59',
            Balance: '9999999990',
            Flags: 0,
            OwnerCount: 1,
            Sequence: 2
          },
          LedgerEntryType: 'AccountRoot',
          LedgerIndex: '4F83A2CF7E70F77F79A307E6A472BFC2585B806A70833CCD1C26105BAE0D6E05',
          PreviousFields: {
            Balance: '10000000000',
            OwnerCount: 0,
            Sequence: 1
          },
          PreviousTxnID: 'B24159F8552C355D35E43623F0E5AD965ADBF034D482421529E2703904E1EC09',
          PreviousTxnLgrSeq: 16154
        }
      }]
    });

    assert(book.isBalanceChangeNode(meta.getNodes()[0]));
  });

  it('Check is balance change node - native currency - not balance change', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'BTC'
    });

    const meta = new Meta({
      AffectedNodes: [{
        ModifiedNode: {
          FinalFields: {
            Account: 'r3kmLJN5D28dHuH8vZNUZpMC43pEHpaocV',
            Balance: '78991384535796',
            Flags: 0,
            OwnerCount: 3,
            Sequence: 188
          },
          LedgerEntryType: 'AccountRoot',
          LedgerIndex: 'B33FDD5CF3445E1A7F2BE9B06336BEBD73A5E3EE885D3EF93F7E3E2992E46F1A',
          PreviousTxnID: 'E9E1988A0F061679E5D14DE77DB0163CE0BBDC00F29E396FFD1DA0366E7D8904',
          PreviousTxnLgrSeq: 195455
        }
      }]
    });

    assert(!book.isBalanceChangeNode(meta.getNodes()[0]));
  });

  it('Parse account balance from node', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    const meta = new Meta({
      AffectedNodes: [
        {
          ModifiedNode: {
            FinalFields: {
              Balance: {
                currency: 'USD',
                issuer: addresses.ISSUER,
                value: '10'
              },
              Flags: 131072,
              HighLimit: {
                currency: 'USD',
                issuer: addresses.ISSUER,
                value: '100'
              },
              HighNode: '0000000000000000',
              LowLimit: {
                currency: 'USD',
                issuer: 'r3PDtZSa5LiYp1Ysn1vMuMzB59RzV3W9QH',
                value: '0'
              },
              LowNode: '0000000000000000'
            },
            LedgerEntryType: 'RippleState',
            LedgerIndex: 'EA4BF03B4700123CDFFB6EB09DC1D6E28D5CEB7F680FB00FC24BC1C3BB2DB959',
            PreviousFields: {
              Balance: {
                currency: 'USD',
                issuer: addresses.ISSUER,
                value: '0'
              }
            },
            PreviousTxnID: '53354D84BAE8FDFC3F4DA879D984D24B929E7FEB9100D2AD9EFCD2E126BCCDC8',
            PreviousTxnLgrSeq: 343570
          }
        },
        {
          ModifiedNode: {
            FinalFields: {
              Balance: {
                currency: 'USD',
                issuer: addresses.ISSUER,
                value: '-10'
              },
              Flags: 131072,
              HighLimit: {
                currency: 'USD',
                issuer: 'r3PDtZSa5LiYp1Ysn1vMuMzB59RzV3W9QH',
                value: '100'
              },
              HighNode: '0000000000000000',
              LowLimit: {
                currency: 'USD',
                issuer: addresses.ISSUER,
                value: '0'
              },
              LowNode: '0000000000000000'
            },
            LedgerEntryType: 'RippleState',
            LedgerIndex: 'EA4BF03B4700123CDFFB6EB09DC1D6E28D5CEB7F680FB00FC24BC1C3BB2DB959',
            PreviousFields: {
              Balance: {
                currency: 'USD',
                issuer: addresses.ISSUER,
                value: '0'
              }
            },
            PreviousTxnID: '53354D84BAE8FDFC3F4DA879D984D24B929E7FEB9100D2AD9EFCD2E126BCCDC8',
            PreviousTxnLgrSeq: 343570
          }
        }
      ]
    });

    assert.deepEqual(book.parseAccountBalanceFromNode(meta.getNodes()[0]), {
      account: 'r3PDtZSa5LiYp1Ysn1vMuMzB59RzV3W9QH',
      balance: '10'
    });

    assert.deepEqual(book.parseAccountBalanceFromNode(meta.getNodes()[1]), {
      account: 'r3PDtZSa5LiYp1Ysn1vMuMzB59RzV3W9QH',
      balance: '10'
    });
  });

  it('Parse account balance from node - native currency', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    const meta = new Meta({
      AffectedNodes: [{
        ModifiedNode: {
          FinalFields: {
            Account: 'r9cZA1mLK5R5Am25ArfXFmqgNwjZgnfk59',
            Balance: '9999999990',
            Flags: 0,
            OwnerCount: 1,
            Sequence: 2
          },
          LedgerEntryType: 'AccountRoot',
          LedgerIndex: '4F83A2CF7E70F77F79A307E6A472BFC2585B806A70833CCD1C26105BAE0D6E05',
          PreviousFields: {
            Balance: '10000000000',
            OwnerCount: 0,
            Sequence: 1
          },
          PreviousTxnID: 'B24159F8552C355D35E43623F0E5AD965ADBF034D482421529E2703904E1EC09',
          PreviousTxnLgrSeq: 16154
        }
      }]
    });

    assert.deepEqual(book.parseAccountBalanceFromNode(meta.getNodes()[0]), {
      account: 'r9cZA1mLK5R5Am25ArfXFmqgNwjZgnfk59',
      balance: '9999999990'
    });
  });

  it('Update funded amounts', function(done) {
    let receivedChangedEvents = 0;
    let receivedFundsChangedEvents = 0;

    const remote = new Remote();

    const message = fixtures.transactionWithRippleState();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._issuerTransferRate = 1000000000;

    book._offers = fixtures.fiatOffers();

    book.on('offer_changed', function() {
      receivedChangedEvents += 1;
    });

    book.on('offer_funds_changed', function(offer, previousFunds, newFunds) {
      assert.strictEqual(previousFunds, '100');
      assert.strictEqual(newFunds, offer.taker_gets_funded);
      assert.notStrictEqual(previousFunds, newFunds);
      switch (++receivedFundsChangedEvents) {
        case 1:
          assert.strictEqual(offer.is_fully_funded, false);
          assert.strictEqual(offer.taker_gets_funded, '10');
          assert.strictEqual(offer.taker_pays_funded, '1954238072');
          break;
        case 2:
          assert.strictEqual(offer.is_fully_funded, false);
          assert.strictEqual(offer.taker_gets_funded, '0');
          assert.strictEqual(offer.taker_pays_funded, '0');
          break;
      }
    });

    book._ownerFunds[addresses.ACCOUNT] = '20';
    book.updateFundedAmounts(message);

    setImmediate(function() {
      assert.strictEqual(book.getOwnerFunds(addresses.ACCOUNT).to_text(), fixtures.FIAT_BALANCE);
      assert.strictEqual(receivedChangedEvents, 2);
      assert.strictEqual(receivedFundsChangedEvents, 2);
      done();
    });
  });

  it('Update funded amounts - increase funds', function() {
    let receivedFundsChangedEvents = 0;

    const remote = new Remote();

    const message = fixtures.transactionWithRippleState({
      balance: '50'
    });

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.fiatOffers({
      account_funds: '19'
    }));

    book.on('offer_funds_changed', function(offer, previousFunds, newFunds) {
      assert.strictEqual(newFunds, offer.taker_gets_funded);
      assert.notStrictEqual(previousFunds, newFunds);
      switch (++receivedFundsChangedEvents) {
        case 1:
          assert.strictEqual(previousFunds, '19');
          assert.strictEqual(offer.is_fully_funded, true);
          assert.strictEqual(offer.taker_gets_funded, fixtures.TAKER_GETS);
          assert.strictEqual(offer.taker_pays_funded, fixtures.TAKER_PAYS);
          break;
        case 2:
          assert.strictEqual(previousFunds, '0');
          assert.strictEqual(offer.is_fully_funded, true);
          assert.strictEqual(offer.taker_gets_funded, '4.9656112525');
          assert.strictEqual(offer.taker_pays_funded, '972251352');
          break;
      }
    });

    book.updateFundedAmounts(message);
  });

  it('Update funded amounts - owner_funds', function(done) {
    const remote = new Remote();

    const message = fixtures.transactionWithRippleState();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._issuerTransferRate = 1002000000;

    book._offers = fixtures.fiatOffers();

    book._ownerFunds[addresses.ACCOUNT] = '100';
    book.updateFundedAmounts(message);

    setImmediate(function() {
      assert.strictEqual(book._offers[0].owner_funds, fixtures.FIAT_BALANCE);
      assert.strictEqual(book._offers[1].owner_funds, fixtures.FIAT_BALANCE);

      done();
    });
  });

  it('Update funded amounts - issuer transfer rate set', function(done) {
    const remote = new Remote();

    const message = fixtures.transactionWithRippleState();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._issuerTransferRate = 1002000000;

    book._ownerFunds[addresses.ACCOUNT] = '100';
    book._offers = fixtures.fiatOffers();

    book.updateFundedAmounts(message);

    setImmediate(function() {
      assert.strictEqual(book.getOwnerFunds(addresses.ACCOUNT).to_text(), '9.980039920159681');

      done();
    });
  });

  it('Update funded amounts - native currency', function(done) {
    let receivedChangedEvents = 0;
    let receivedFundsChangedEvents = 0;

    const remote = new Remote();

    const message = fixtures.transactionWithAccountRoot();

    const book = remote.createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'USD'
    });

    book._offers = fixtures.NATIVE_OFFERS;

    book.on('offer_changed', function() {
      receivedChangedEvents += 1;
    });

    book.on('offer_funds_changed', function(offer, previousFunds, newFunds) {
      assert.strictEqual(previousFunds, fixtures.NATIVE_BALANCE_PREVIOUS);
      assert.strictEqual(newFunds, offer.taker_gets_funded);
      assert.notStrictEqual(previousFunds, newFunds);
      switch (++receivedFundsChangedEvents) {
        case 1:
          assert(offer.is_fully_funded);
          break;
        case 2:
          assert(!offer.is_fully_funded);
          break;
      }
    });

    book._ownerFunds[addresses.ACCOUNT] = fixtures.NATIVE_BALANCE_PREVIOUS;
    book.updateFundedAmounts(message);

    setImmediate(function() {
      book.getOwnerFunds(addresses.ACCOUNT, fixtures.NATIVE_BALANCE);
      assert.strictEqual(receivedChangedEvents, 2);
      assert.strictEqual(receivedFundsChangedEvents, 2);
      done();
    });
  });

  it('Update funded amounts - no affected account', function(done) {
    const remote = new Remote();

    const message = fixtures.transactionWithAccountRoot({
      account: addresses.ACCOUNT
    });

    const book = remote.createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'USD'
    });

    book._offers = fixtures.NATIVE_OFFERS;

    book._offers.__defineGetter__(0, function() {
      assert(false, 'Iteration of offers for unaffected account');
    });

    book.on('offer_changed', function() {
      assert(false, 'offer_changed event emitted');
    });

    book.on('offer_funds_changed', function() {
      assert(false, 'offer_funds_changed event emitted');
    });

    book.updateFundedAmounts(message);

    setImmediate(done);
  });

  it('Update funded amounts - no balance change', function(done) {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'XRP',
      issuer_pays: addresses.ISSUER,
      currency_pays: 'USD'
    });

    const message = fixtures.transactionWithInvalidAccountRoot();

    book._offers = fixtures.NATIVE_OFFERS;

    book.on('offer_changed', function() {
      assert(false, 'offer_changed event emitted');
    });

    book.on('offer_funds_changed', function() {
      assert(false, 'offer_funds_changed event emitted');
    });

    assert.strictEqual(typeof book.parseAccountBalanceFromNode, 'function');

    book.parseAccountBalanceFromNode = function() {
      assert(false, 'getBalanceChange should not be called');
    };

    book._ownerFunds[addresses.ACCOUNT] = '100';
    book.updateFundedAmounts(message);

    setImmediate(done);
  });

  it('Update funded amounts - deferred TransferRate', function(done) {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    const message = fixtures.transactionWithRippleState();

    remote.request = function(request) {
      assert.deepEqual(request.message, {
        command: 'account_info',
        id: undefined,
        account: addresses.ISSUER
      });

      request.emit('success', fixtures.accountInfoResponse());

      assert.strictEqual(book._issuerTransferRate, fixtures.TRANSFER_RATE);
      done();
    };

    book._ownerFunds[addresses.ACCOUNT] = '100';
    book.updateFundedAmounts(message);
  });

  it('Set offers - issuer transfer rate set - iou/xrp', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._issuerTransferRate = 1002000000;

    const offers = fixtures.bookOffersResponse().offers;

    book.setOffers(offers);

    assert.strictEqual(book._offers.length, 5);

    assert.strictEqual(book.getOwnerOfferTotal(addresses.ACCOUNT).to_text(), '275.85192574');
    assert.strictEqual(book.getOwnerOfferTotal(addresses.OTHER_ACCOUNT).to_text(), '24.060765960393');
    assert.strictEqual(book.getOwnerOfferTotal(addresses.THIRD_ACCOUNT).to_text(), '712.60995');
    assert.strictEqual(book.getOwnerOfferTotal(addresses.FOURTH_ACCOUNT).to_text(), '288.08');

    assert.strictEqual(book.getOwnerOfferCount(addresses.ACCOUNT), 2);
    assert.strictEqual(book.getOwnerOfferCount(addresses.OTHER_ACCOUNT), 1);
    assert.strictEqual(book.getOwnerOfferCount(addresses.THIRD_ACCOUNT), 1);
    assert.strictEqual(book.getOwnerOfferCount(addresses.FOURTH_ACCOUNT), 1);

    assert.strictEqual(book.getOwnerFunds(addresses.ACCOUNT).to_text(), '2006.015671538605');
    assert.strictEqual(book.getOwnerFunds(addresses.OTHER_ACCOUNT).to_text(), '24.01284027983332');
    assert.strictEqual(book.getOwnerFunds(addresses.THIRD_ACCOUNT).to_text(), '9053.294314019701');
    assert.strictEqual(book.getOwnerFunds(addresses.FOURTH_ACCOUNT).to_text(), '7229.594289344439');
  });

  it('Set offers - issuer transfer rate set - iou/xrp - funded amounts', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._issuerTransferRate = 1002000000;

    const offers = fixtures.bookOffersResponse({
      account_funds: '233.13532'
    }).offers;

    book.setOffers(offers);

    const offerOneTakerGetsFunded = Amount.from_json({
      value: book._offers[0].taker_gets_funded,
      currency: 'USD',
      issuer: addresses.ISSUER
    });

    const offerOneTakerGetsFundedExpected = Amount.from_json({
      value: '79.39192374',
      currency: 'USD',
      issuer: addresses.ISSUER
    });

    assert.strictEqual(offerOneTakerGetsFunded.equals(offerOneTakerGetsFundedExpected), true);
    assert.strictEqual(book._offers[0].is_fully_funded, true);

    const offerTwoTakerGetsFunded = Amount.from_json({
      value: book._offers[1].taker_gets_funded,
      currency: 'USD',
      issuer: addresses.ISSUER
    });

    const offerTwoTakerGetsFundedExpected = Amount.from_json({
      value: '24.01284027983332',
      currency: 'USD',
      issuer: addresses.ISSUER
    });

    const offerTwoTakerPaysFunded = Amount.from_json(book._offers[1].taker_pays_funded);

    const offerTwoTakerPaysFundedExpected = Amount.from_json('1661400177');

    assert.strictEqual(offerTwoTakerGetsFunded.equals(offerTwoTakerGetsFundedExpected), true);
    assert.strictEqual(offerTwoTakerPaysFunded.equals(offerTwoTakerPaysFundedExpected), true);
    assert.strictEqual(book._offers[1].is_fully_funded, false);

    const offerFiveTakerGetsFunded = Amount.from_json({
      value: book._offers[4].taker_gets_funded,
      currency: 'USD',
      issuer: addresses.ISSUER
    });

    const offerFiveTakerGetsFundedExpected = Amount.from_json({
      value: '153.2780562999202',
      currency: 'USD',
      issuer: addresses.ISSUER
    });

    const offerFiveTakerPaysFunded = Amount.from_json(book._offers[4].taker_pays_funded);

    const offerFiveTakerPaysFundedExpected = Amount.from_json('10684615137');

    assert.strictEqual(offerFiveTakerGetsFunded.equals(offerFiveTakerGetsFundedExpected), true);
    assert.strictEqual(offerFiveTakerPaysFunded.equals(offerFiveTakerPaysFundedExpected), true);
    assert.strictEqual(book._offers[4].is_fully_funded, false);
  });

  it('Set offers - multiple calls', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._issuerTransferRate = 1002000000;

    const offers = fixtures.bookOffersResponse().offers;

    book.setOffers(offers);
    book.setOffers(offers);

    assert.strictEqual(book._offers.length, 5);

    assert.strictEqual(book.getOwnerOfferTotal(addresses.ACCOUNT).to_text(), '275.85192574');
    assert.strictEqual(book.getOwnerOfferTotal(addresses.OTHER_ACCOUNT).to_text(), '24.060765960393');
    assert.strictEqual(book.getOwnerOfferTotal(addresses.THIRD_ACCOUNT).to_text(), '712.60995');
    assert.strictEqual(book.getOwnerOfferTotal(addresses.FOURTH_ACCOUNT).to_text(), '288.08');

    assert.strictEqual(book.getOwnerOfferCount(addresses.ACCOUNT), 2);
    assert.strictEqual(book.getOwnerOfferCount(addresses.OTHER_ACCOUNT), 1);
    assert.strictEqual(book.getOwnerOfferCount(addresses.THIRD_ACCOUNT), 1);
    assert.strictEqual(book.getOwnerOfferCount(addresses.FOURTH_ACCOUNT), 1);

    assert.strictEqual(book.getOwnerFunds(addresses.ACCOUNT).to_text(), '2006.015671538605');
    assert.strictEqual(book.getOwnerFunds(addresses.OTHER_ACCOUNT).to_text(), '24.01284027983332');
    assert.strictEqual(book.getOwnerFunds(addresses.THIRD_ACCOUNT).to_text(), '9053.294314019701');
    assert.strictEqual(book.getOwnerFunds(addresses.FOURTH_ACCOUNT).to_text(), '7229.594289344439');
  });

  it('Set offers - incorrect taker pays funded', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._issuerTransferRate = 1002000000;

    const offers = fixtures.DECIMAL_TAKER_PAYS_FUNDED_OFFERS;

    book.setOffers(offers);

    assert.strictEqual(book._offers.length, 1);

    assert.strictEqual(book._offers[0].taker_gets_funded, '9261.514125778347');
    assert.strictEqual(book._offers[0].taker_pays_funded, '1704050437125');
  });

  it('Notify - created node', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._issuerTransferRate = 1002000000;
    book._subscribed = book._synced = true;

    const message = fixtures.transactionWithCreatedOffer();

    book.notify(message);

    assert.strictEqual(book._offers.length, 1);
    assert.strictEqual(book.getOwnerOfferTotal(addresses.ACCOUNT).to_text(), '1.9951');
    assert.strictEqual(book.getOwnerOfferCount(addresses.ACCOUNT), 1);
    assert.strictEqual(book.getOwnerFunds(addresses.ACCOUNT).to_text(), '2006.015671538605');
  });

  it('Notify - created nodes - correct sorting', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._issuerTransferRate = 1002000000;
    book._subscribed = book._synced = true;

    const offer = fixtures.transactionWithCreatedOffer();

    const lowQualityOffer = fixtures.transactionWithCreatedOffer({
      account: addresses.OTHER_ACCOUNT,
      amount: '1.5'
    });

    const highQualityOffer = fixtures.transactionWithCreatedOffer({
      account: addresses.THIRD_ACCOUNT,
      amount: '3.83'
    });

    book.notify(offer);
    book.notify(lowQualityOffer);
    book.notify(highQualityOffer);

    assert.strictEqual(book._offers.length, 3);
    assert.strictEqual(book._offers[0].Account, addresses.THIRD_ACCOUNT);
    assert.strictEqual(book._offers[1].Account, addresses.ACCOUNT);
    assert.strictEqual(book._offers[2].Account, addresses.OTHER_ACCOUNT);
  });

  it('Notify - created nodes - events', function() {
    let numTransactionEvents = 0;
    let numModelEvents = 0;
    let numOfferAddedEvents = 0;

    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book.on('transaction', function() {
      numTransactionEvents += 1;
    });

    book.on('model', function() {
      numModelEvents += 1;
    });

    book.on('offer_added', function() {
      numOfferAddedEvents += 1;
    });

    book._issuerTransferRate = 1002000000;
    book._subscribed = book._synced = true;

    const offer = fixtures.transactionWithCreatedOffer();
    const offer2 = fixtures.transactionWithCreatedOffer();
    const offer3 = fixtures.transactionWithCreatedOffer();

    book.notify(offer);
    book.notify(offer2);
    book.notify(offer3);

    assert.strictEqual(numTransactionEvents, 3);
    assert.strictEqual(numModelEvents, 3);
    assert.strictEqual(numOfferAddedEvents, 3);
  });

  it('Notify - deleted node', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._subscribed = true;
    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.fiatOffers());

    const message = fixtures.transactionWithDeletedOffer();

    book.notify(message);

    assert.strictEqual(book._offers.length, 2);
    assert.strictEqual(book.getOwnerOfferTotal(addresses.ACCOUNT).to_text(), '4.9656112525');
    assert.strictEqual(book.getOwnerOfferCount(addresses.ACCOUNT), 1);
  });

  it('Notify - deleted node - last offer', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._subscribed = true;
    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.fiatOffers().slice(0, 1));

    const message = fixtures.transactionWithDeletedOffer();

    book.notify(message);

    assert.strictEqual(book._offers.length, 0);
    assert.strictEqual(book.getOwnerFunds(addresses.ACCOUNT), undefined);
  });

  it('Notify - deleted node - events', function() {
    let numTransactionEvents = 0;
    let numModelEvents = 0;
    let numTradeEvents = 0;
    let numOfferRemovedEvents = 0;

    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book.on('transaction', function() {
      numTransactionEvents += 1;
    });

    book.on('model', function() {
      numModelEvents += 1;
    });

    book.on('trade', function() {
      numTradeEvents += 1;
    });

    book.on('offer_removed', function() {
      numOfferRemovedEvents += 1;
    });

    book._subscribed = true;
    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.fiatOffers());

    const message = fixtures.transactionWithDeletedOffer();

    book.notify(message);

    assert.strictEqual(numTransactionEvents, 1);
    assert.strictEqual(numModelEvents, 1);
    assert.strictEqual(numTradeEvents, 1);
    assert.strictEqual(numOfferRemovedEvents, 1);
  });

  it('Notify - deleted node - trade', function(done) {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book.on('trade', function(tradePays, tradeGets) {
      const expectedTradePays = Amount.from_json(fixtures.TAKER_PAYS);
      const expectedTradeGets = Amount.from_json({
        value: fixtures.TAKER_GETS,
        currency: 'USD',
        issuer: addresses.ISSUER
      });

      assert(tradePays.equals(expectedTradePays));
      assert(tradeGets.equals(expectedTradeGets));

      done();
    });

    book._subscribed = true;
    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.fiatOffers());

    const message = fixtures.transactionWithDeletedOffer();

    book.notify(message);
  });

  it('Notify - deleted node - offer cancel', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._subscribed = true;
    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.fiatOffers());

    const message = fixtures.transactionWithDeletedOffer({
      transaction_type: 'OfferCancel'
    });

    book.notify(message);

    assert.strictEqual(book._offers.length, 2);
    assert.strictEqual(book.getOwnerOfferTotal(addresses.ACCOUNT).to_text(), '4.9656112525');
    assert.strictEqual(book.getOwnerOfferCount(addresses.ACCOUNT), 1);
  });

  it('Notify - deleted node - offer cancel - last offer', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._subscribed = true;
    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.fiatOffers().slice(0, 1));

    const message = fixtures.transactionWithDeletedOffer({
      transaction_type: 'OfferCancel'
    });

    book.notify(message);

    assert.strictEqual(book._offers.length, 0);
    assert.strictEqual(book.getOwnerFunds(addresses.ACCOUNT), undefined);
  });

  it('Notify - modified node', function() {
    const remote = new Remote();

    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._subscribed = true;
    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.fiatOffers());

    const message = fixtures.transactionWithModifiedOffer();

    book.notify(message);

    assert.strictEqual(book._offers.length, 3);
    assert.strictEqual(book.getOwnerOfferTotal(addresses.ACCOUNT).to_text(), '23.8114145625');
    assert.strictEqual(book.getOwnerOfferCount(addresses.ACCOUNT), 2);

    assert.strictEqual(book._offers[0].is_fully_funded, true);
    assert.strictEqual(book._offers[0].taker_gets_funded, fixtures.TAKER_GETS_FINAL);
    assert.strictEqual(book._offers[0].taker_pays_funded, fixtures.TAKER_PAYS_FINAL);

    assert.strictEqual(book._offers[1].is_fully_funded, true);
    assert.strictEqual(book._offers[1].taker_gets_funded, '4.9656112525');
    assert.strictEqual(book._offers[1].taker_pays_funded, '972251352');
  });

  it('Notify - modified node - events', function() {
    let numTransactionEvents = 0;
    let numModelEvents = 0;
    let numTradeEvents = 0;
    let numOfferChangedEvents = 0;

    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book.on('transaction', function() {
      numTransactionEvents += 1;
    });

    book.on('model', function() {
      numModelEvents += 1;
    });

    book.on('trade', function() {
      numTradeEvents += 1;
    });

    book.on('offer_changed', function() {
      numOfferChangedEvents += 1;
    });

    book._subscribed = true;
    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.fiatOffers());

    const message = fixtures.transactionWithModifiedOffer();

    book.notify(message);

    assert.strictEqual(numTransactionEvents, 1);
    assert.strictEqual(numModelEvents, 1);
    assert.strictEqual(numTradeEvents, 1);
    assert.strictEqual(numOfferChangedEvents, 1);
  });

  it('Notify - modified node - trade', function(done) {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book.on('trade', function(tradePays, tradeGets) {
      const expectedTradePays = Amount.from_json('800000000');
      const expectedTradeGets = Amount.from_json({
        value: 1,
        currency: 'USD',
        issuer: addresses.ISSUER
      });

      assert(tradePays.equals(expectedTradePays));
      assert(tradeGets.equals(expectedTradeGets));

      done();
    });

    book._subscribed = true;
    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.fiatOffers());

    const message = fixtures.transactionWithModifiedOffer();

    book.notify(message);
  });

  it('Notify - modified nodes - trade', function(done) {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book.on('trade', function(tradePays, tradeGets) {
      const expectedTradePays = Amount.from_json('870000000');
      const expectedTradeGets = Amount.from_json({
        value: 2,
        currency: 'USD',
        issuer: addresses.ISSUER
      });

      assert(tradePays.equals(expectedTradePays));
      assert(tradeGets.equals(expectedTradeGets));

      done();
    });

    book._subscribed = true;
    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.fiatOffers());

    const message = fixtures.transactionWithModifiedOffers();

    book.notify(message);
  });

  it('Notify - no nodes', function() {
    let numTransactionEvents = 0;
    let numModelEvents = 0;
    let numTradeEvents = 0;
    let numOfferChangedEvents = 0;

    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book.on('transaction', function() {
      numTransactionEvents += 1;
    });

    book.on('model', function() {
      numModelEvents += 1;
    });

    book.on('trade', function() {
      numTradeEvents += 1;
    });

    book.on('offer_changed', function() {
      numOfferChangedEvents += 1;
    });

    book._subscribed = true;
    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.fiatOffers());

    const message = fixtures.transactionWithNoNodes();

    book.notify(message);

    assert.strictEqual(numTransactionEvents, 0);
    assert.strictEqual(numModelEvents, 0);
    assert.strictEqual(numTradeEvents, 0);
    assert.strictEqual(numOfferChangedEvents, 0);
  });

  it('Notify - in disconnected state', function(done) {
    this.timeout(100);

    const remote = new Remote();
    const transaction = fixtures.transactionWithDeletedOfferR();
    const offers = {
      offers: fixtures.REQUEST_OFFERS_NATIVE
    };

    function handleRequest(request) {
      switch (request.message.command) {
        case 'book_offers':
          request.emit('success', offers);
          break;
        case 'subscribe':
          request.emit('success', {});
          break;
      }
    }

    remote.request = function(request) {
      setImmediate(function() {
        handleRequest(request);
      });
    };

    const book = remote.createOrderBook({
      currency_gets: 'XRP',
      currency_pays: 'USD',
      issuer_pays: addresses.ISSUER
    });

    book.on('model', function() {
      remote.emit('disconnect');

      // Check cache reset
      assert.strictEqual(book._synced, false);
      assert.deepEqual(book._ownerFunds, {});
      assert.deepEqual(book._ownerOffersTotal, {});

      setTimeout(function() {
        // Notification should be dropped if not synced
        book.notify(transaction);
        done();
      }, 10);
    });
  });
  it('Delete offer - offer cancel - funded after delete', function() {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._subscribed = true;
    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.fiatOffers({
      account_funds: '20'
    }));

    book.deleteOffer(fixtures.transactionWithDeletedOffer({
      transaction_type: 'OfferCancel'
    }).mmeta.getNodes()[0], true);

    assert.strictEqual(book._offers.length, 2);
    assert.strictEqual(book.getOwnerOfferCount(addresses.ACCOUNT), 1);
    assert.strictEqual(book.getOwnerOfferCount(addresses.OTHER_ACCOUNT), 1);
    assert.strictEqual(book.getOwnerOfferTotal(addresses.ACCOUNT).to_text(), '4.9656112525');

    assert.strictEqual(book._offers[0].is_fully_funded, true);
  });

  it('Delete offer - offer cancel - not fully funded after delete', function() {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._subscribed = true;
    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.fiatOffers({
      account_funds: '4.5'
    }));

    book.deleteOffer(fixtures.transactionWithDeletedOffer({
      transaction_type: 'OfferCancel'
    }).mmeta.getNodes()[0], true);

    assert.strictEqual(book._offers.length, 2);
    assert.strictEqual(book.getOwnerOfferCount(addresses.ACCOUNT), 1);
    assert.strictEqual(book.getOwnerOfferCount(addresses.OTHER_ACCOUNT), 1);
    assert.strictEqual(book.getOwnerOfferTotal(addresses.ACCOUNT).to_text(), '4.9656112525');

    assert.strictEqual(book._offers[0].is_fully_funded, false);
    assert.strictEqual(book._offers[0].taker_gets_funded, '4.5');
    assert.strictEqual(book._offers[0].taker_pays_funded, '881086106');
  });

  it('Insert offer - best quality', function() {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._subscribed = true;
    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.QUALITY_OFFERS);

    book.insertOffer(fixtures.transactionWithCreatedOffer({
      amount: '51.04587961502088'
    }).mmeta.getNodes()[0]);

    assert.strictEqual(book._offers.length, 2);

    assert.strictEqual(book._offers[0].taker_gets_funded, '51.04587961502088');
    assert.strictEqual(book._offers[0].taker_pays_funded, fixtures.TAKER_PAYS);
    assert.strictEqual(book._offers[0].quality, '75977580.74206542');
  });

  it('Insert offer - best quality - insufficient funds for all offers', function() {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._subscribed = true;
    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.fiatOffers());

    book.insertOffer(fixtures.transactionWithCreatedOffer({
      amount: '298'
    }).mmeta.getNodes()[0]);

    assert.strictEqual(book._offers.length, 4);
    assert.strictEqual(book.getOwnerOfferCount(addresses.ACCOUNT), 3);
    assert.strictEqual(book.getOwnerOfferCount(addresses.OTHER_ACCOUNT), 1);
    assert.strictEqual(book.getOwnerOfferTotal(addresses.ACCOUNT).to_text(), '322.8114145625');

    assert.strictEqual(book._offers[0].is_fully_funded, true);
    assert.strictEqual(book._offers[0].taker_gets_funded, '298');
    assert.strictEqual(book._offers[0].taker_pays_funded, fixtures.TAKER_PAYS);

    assert.strictEqual(book._offers[1].is_fully_funded, true);
    assert.strictEqual(book._offers[1].taker_gets_funded, fixtures.TAKER_GETS);
    assert.strictEqual(book._offers[1].taker_pays_funded, fixtures.TAKER_PAYS);

    assert.strictEqual(book._offers[2].is_fully_funded, false);
    assert.strictEqual(book._offers[2].taker_gets_funded, '0.5185677538508');
    assert.strictEqual(book._offers[2].taker_pays_funded, '101533965');
  });

  it('Insert offer - worst quality - insufficient funds for all orders', function() {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._subscribed = true;
    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.fiatOffers({
      account_funds: '25'
    }));

    book.insertOffer(fixtures.transactionWithCreatedOffer({
      amount: '5'
    }).mmeta.getNodes()[0]);

    assert.strictEqual(book._offers.length, 4);
    assert.strictEqual(book.getOwnerOfferCount(addresses.ACCOUNT), 3);
    assert.strictEqual(book.getOwnerOfferCount(addresses.OTHER_ACCOUNT), 1);
    assert.strictEqual(book.getOwnerOfferTotal(addresses.ACCOUNT).to_text(), '29.8114145625');

    assert.strictEqual(book._offers[0].is_fully_funded, true);
    assert.strictEqual(book._offers[0].taker_gets_funded, fixtures.TAKER_GETS);
    assert.strictEqual(book._offers[0].taker_pays_funded, fixtures.TAKER_PAYS);

    assert.strictEqual(book._offers[1].is_fully_funded, true);
    assert.strictEqual(book._offers[1].taker_gets_funded, '4.9656112525');
    assert.strictEqual(book._offers[1].taker_pays_funded, '972251352');

    assert.strictEqual(book._offers[3].is_fully_funded, false);
    assert.strictEqual(book._offers[3].taker_gets_funded, '0.1885854375');
    assert.strictEqual(book._offers[3].taker_pays_funded, '146279781');
  });

  it('Insert offer - middle quality - insufficient funds for all offers', function() {
    const remote = new Remote();
    const book = remote.createOrderBook({
      currency_gets: 'USD',
      issuer_gets: addresses.ISSUER,
      currency_pays: 'XRP'
    });

    book._subscribed = true;
    book._issuerTransferRate = 1000000000;

    book.setOffers(fixtures.fiatOffers({
      account_funds: '30'
    }));

    book.insertOffer(fixtures.transactionWithCreatedOffer({
      amount: '19.84080331'
    }).mmeta.getNodes()[0]);

    assert.strictEqual(book._offers.length, 4);
    assert.strictEqual(book.getOwnerOfferCount(addresses.ACCOUNT), 3);
    assert.strictEqual(book.getOwnerOfferCount(addresses.OTHER_ACCOUNT), 1);
    assert.strictEqual(book.getOwnerOfferTotal(addresses.ACCOUNT).to_text(), '44.6522178725');

    assert.strictEqual(book._offers[0].is_fully_funded, true);
    assert.strictEqual(book._offers[0].taker_gets_funded, fixtures.TAKER_GETS);
    assert.strictEqual(book._offers[0].taker_pays_funded, fixtures.TAKER_PAYS);

    assert.strictEqual(book._offers[1].is_fully_funded, false);
    assert.strictEqual(book._offers[1].taker_gets_funded, '10.15419669');
    assert.strictEqual(book._offers[1].taker_pays_funded, '1984871849');

    assert.strictEqual(book._offers[2].is_fully_funded, false);
    assert.strictEqual(book._offers[2].taker_gets_funded, '0');
    assert.strictEqual(book._offers[2].taker_pays_funded, '0');
  });

  it('Request offers - native currency', function(done) {
    const remote = new Remote();

    const offers = {
      offers: fixtures.REQUEST_OFFERS_NATIVE
    };

    const expected = [
      {
        Account: addresses.ACCOUNT,
        BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC985711A3A4254F5000',
        BookNode: '0000000000000000',
        Flags: 131072,
        LedgerEntryType: 'Offer',
        OwnerNode: '0000000000000000',
        Sequence: 195,
        TakerGets: '1000',
        TakerPays: {
          currency: 'USD',
          issuer: addresses.ISSUER,
          value: '56.06639660617357'
        },
        index: 'B6BC3B0F87976370EE11F5575593FE63AA5DC1D602830DC96F04B2D597F044BF',
        owner_funds: '600',
        is_fully_funded: false,
        taker_gets_funded: '600',
        taker_pays_funded: '33.6398379637041',
        quality: '.0560663966061735'
      },
      {
        Account: addresses.OTHER_ACCOUNT,
        BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC985711B6D8C62EF414',
        BookNode: '0000000000000000',
        Expiration: 461498565,
        Flags: 131072,
        LedgerEntryType: 'Offer',
        OwnerNode: '0000000000000144',
        Sequence: 29354,
        TakerGets: '2000',
        TakerPays: {
          currency: 'USD',
          issuer: addresses.ISSUER,
          value: '99.72233516476456'
        },
        index: 'A437D85DF80D250F79308F2B613CF5391C7CF8EE9099BC4E553942651CD9FA86',
        owner_funds: '4000',
        is_fully_funded: true,
        taker_gets_funded: '2000',
        taker_pays_funded: '99.72233516476456',
        quality: '0.049861167582382'
      },
      {
        Account: addresses.THIRD_ACCOUNT,
        BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC985711B6D8C62EF414',
        BookNode: '0000000000000000',
        Expiration: 461498565,
        Flags: 131072,
        LedgerEntryType: 'Offer',
        OwnerNode: '0000000000000144',
        Sequence: 29356,
        TakerGets: '2000',
        TakerPays: {
          currency: 'USD',
          issuer: addresses.ISSUER,
          value: '99.72233516476456'
        },
        index: 'A437D85DF80D250F79308F2B613CF5391C7CF8EE9099BC4E553942651CD9FA86',
        owner_funds: '3900',
        is_fully_funded: true,
        taker_gets_funded: '2000',
        taker_pays_funded: '99.72233516476456',
        quality: '0.049861167582382'
      },
      {
        Account: addresses.THIRD_ACCOUNT,
        BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC985711B6D8C62EF414',
        BookNode: '0000000000000000',
        Expiration: 461498565,
        Flags: 131078,
        LedgerEntryType: 'Offer',
        OwnerNode: '0000000000000144',
        Sequence: 29354,
        TakerGets: '2000',
        TakerPays: {
          currency: 'USD',
          issuer: addresses.ISSUER,
          value: '99.72233516476456'
        },
        index: 'A437D85DF80D250F79308F2B613CF5391C7CF8EE9099BC4E553942651CD9FA86',
        is_fully_funded: false,
        taker_gets_funded: '1900',
        taker_pays_funded: '94.7362184065258',
        owner_funds: '3900',
        quality: '0.049861167582382'
      }
    ];

    remote.request = function(request) {
      switch (request.message.command) {
        case 'book_offers':
          assert.deepEqual(request.message, {
            command: 'book_offers',
            id: undefined,
            taker_gets: {
              currency: '0000000000000000000000000000000000000000'
            },
            taker_pays: {
              currency: '0000000000000000000000005553440000000000',
              issuer: addresses.ISSUER
            },
            taker: 'rrrrrrrrrrrrrrrrrrrrBZbvji',
            ledger_index: 'validated'
          });

          setImmediate(function() {
            request.emit('success', offers);
          });
          break;
      }
    };

    const book = remote.createOrderBook({
      currency_gets: 'XRP',
      currency_pays: 'USD',
      issuer_pays: addresses.ISSUER
    });

    book.on('model', function(model) {
      assert.deepEqual(model, expected);
      assert.strictEqual(book._synced, true);
      done();
    });
  });
});
