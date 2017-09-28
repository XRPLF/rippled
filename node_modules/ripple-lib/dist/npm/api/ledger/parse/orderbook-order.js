
'use strict';
var _ = require('lodash');
var utils = require('./utils');
var flags = utils.core.Remote.flags.offer;
var parseAmount = require('./amount');

function parseOrderbookOrder(order) {
  var direction = (order.Flags & flags.Sell) === 0 ? 'buy' : 'sell';
  var takerGetsAmount = parseAmount(order.TakerGets);
  var takerPaysAmount = parseAmount(order.TakerPays);
  var quantity = direction === 'buy' ? takerPaysAmount : takerGetsAmount;
  var totalPrice = direction === 'buy' ? takerGetsAmount : takerPaysAmount;

  // note: immediateOrCancel and fillOrKill orders cannot enter the order book
  // so we can omit those flags here
  var specification = utils.removeUndefined({
    direction: direction,
    quantity: quantity,
    totalPrice: totalPrice,
    passive: (order.Flags & flags.Passive) !== 0 || undefined
  });

  var properties = {
    maker: order.Account,
    sequence: order.Sequence,
    makerExchangeRate: utils.adjustQualityForXRP(order.quality, takerGetsAmount.currency, takerPaysAmount.currency)
  };

  var takerGetsFunded = order.taker_gets_funded ? parseAmount(order.taker_gets_funded) : undefined;
  var takerPaysFunded = order.taker_pays_funded ? parseAmount(order.taker_pays_funded) : undefined;
  var available = utils.removeUndefined({
    fundedAmount: takerGetsFunded,
    priceOfFundedAmount: takerPaysFunded
  });
  var state = _.isEmpty(available) ? undefined : available;
  return utils.removeUndefined({ specification: specification, properties: properties, state: state });
}

module.exports = parseOrderbookOrder;