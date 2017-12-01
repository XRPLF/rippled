
'use strict';
var assert = require('assert');
var utils = require('./utils');
var parseAmount = require('./amount');
var flags = utils.core.Transaction.flags.OfferCreate;

function parseOrder(tx) {
  assert(tx.TransactionType === 'OfferCreate');

  var direction = (tx.Flags & flags.Sell) === 0 ? 'buy' : 'sell';
  var takerGetsAmount = parseAmount(tx.TakerGets);
  var takerPaysAmount = parseAmount(tx.TakerPays);
  var quantity = direction === 'buy' ? takerPaysAmount : takerGetsAmount;
  var totalPrice = direction === 'buy' ? takerGetsAmount : takerPaysAmount;

  return utils.removeUndefined({
    direction: direction,
    quantity: quantity,
    totalPrice: totalPrice,
    passive: (tx.Flags & flags.Passive) !== 0 || undefined,
    immediateOrCancel: (tx.Flags & flags.ImmediateOrCancel) !== 0 || undefined,
    fillOrKill: (tx.Flags & flags.FillOrKill) !== 0 || undefined
  });
}

module.exports = parseOrder;