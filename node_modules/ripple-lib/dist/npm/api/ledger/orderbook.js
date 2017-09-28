
'use strict';
var _ = require('lodash');
var async = require('async');
var utils = require('./utils');
var validate = utils.common.validate;
var composeAsync = utils.common.composeAsync;
var convertErrors = utils.common.convertErrors;
var parseOrderbookOrder = require('./parse/orderbook-order');

// account is to specify a "perspective", which affects which unfunded offers
// are returned
function getBookOffers(remote, account, ledgerVersion, limit, takerGets, takerPays, callback) {
  remote.requestBookOffers(utils.renameCounterpartyToIssuerInOrder({
    taker_gets: takerGets,
    taker_pays: takerPays,
    ledger: ledgerVersion || 'validated',
    limit: limit,
    taker: account
  }), composeAsync(function (data) {
    return data.offers;
  }, convertErrors(callback)));
}

function isSameIssue(a, b) {
  return a.currency === b.currency && a.counterparty === b.counterparty;
}

function directionFilter(direction, order) {
  return order.specification.direction === direction;
}

function flipOrder(order) {
  var specification = order.specification;
  var flippedSpecification = {
    quantity: specification.totalPrice,
    totalPrice: specification.quantity,
    direction: specification.direction === 'buy' ? 'sell' : 'buy'
  };
  var newSpecification = _.merge({}, specification, flippedSpecification);
  return _.merge({}, order, { specification: newSpecification });
}

function alignOrder(base, order) {
  var quantity = order.specification.quantity;
  return isSameIssue(quantity, base) ? order : flipOrder(order);
}

function formatBidsAndAsks(orderbook, offers) {
  // the "base" currency is the currency that you are buying or selling
  // the "counter" is the currency that the "base" is priced in
  // a "bid"/"ask" is an order to buy/sell the base, respectively
  // for bids: takerGets = totalPrice = counter, takerPays = quantity = base
  // for asks: takerGets = quantity = base, takerPays = totalPrice = counter
  // quality = takerPays / takerGets; price = totalPrice / quantity
  // for bids: lowest quality => lowest quantity/totalPrice => highest price
  // for asks: lowest quality => lowest totalPrice/quantity => lowest price
  // for both bids and asks, lowest quality is closest to mid-market
  // we sort the orders so that earlier orders are closer to mid-market
  var orders = _.sortBy(offers, 'quality').map(parseOrderbookOrder);
  var alignedOrders = orders.map(_.partial(alignOrder, orderbook.base));
  var bids = alignedOrders.filter(_.partial(directionFilter, 'buy'));
  var asks = alignedOrders.filter(_.partial(directionFilter, 'sell'));
  return { bids: bids, asks: asks };
}

function getOrderbookAsync(account, orderbook, options, callback) {
  validate.address(account);
  validate.orderbook(orderbook);
  validate.getOrderbookOptions(options);

  var getter = _.partial(getBookOffers, this.remote, account, options.ledgerVersion, options.limit);
  var getOffers = _.partial(getter, orderbook.base, orderbook.counter);
  var getReverseOffers = _.partial(getter, orderbook.counter, orderbook.base);
  async.parallel([getOffers, getReverseOffers], composeAsync(function (data) {
    return formatBidsAndAsks(orderbook, _.flatten(data));
  }, callback));
}

function getOrderbook(account, orderbook) {
  var options = arguments.length <= 2 || arguments[2] === undefined ? {} : arguments[2];

  return utils.promisify(getOrderbookAsync).call(this, account, orderbook, options);
}

module.exports = getOrderbook;