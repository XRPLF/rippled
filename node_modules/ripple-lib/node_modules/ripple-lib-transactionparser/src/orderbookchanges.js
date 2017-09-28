'use strict';
var _ = require('lodash');
var utils = require('./utils');
var BigNumber = require('bignumber.js');
var parseQuality = require('./quality');

var lsfSell = 0x00020000;   // see "lsfSell" flag in rippled source code

function convertOrderChange(order) {
  var takerGets = order.taker_gets;
  var takerPays = order.taker_pays;
  var direction = order.sell ? 'sell' : 'buy';
  var quantity = (direction === 'buy') ? takerPays : takerGets;
  var totalPrice = (direction === 'buy') ? takerGets : takerPays;
  return {
    direction: direction,
    quantity: quantity,
    totalPrice: totalPrice,
    sequence: order.sequence,
    status: order.status,
    makerExchangeRate: order.quality
  };
}

function getQuality(node) {
  var takerGets = node.finalFields.TakerGets || node.newFields.TakerGets;
  var takerPays = node.finalFields.TakerPays || node.newFields.TakerPays;
  var takerGetsCurrency = takerGets.currency || 'XRP';
  var takerPaysCurrency = takerPays.currency || 'XRP';
  var bookDirectory = node.finalFields.BookDirectory
    || node.newFields.BookDirectory;
  var qualityHex = bookDirectory.substring(bookDirectory.length - 16);
  return parseQuality(qualityHex, takerGetsCurrency, takerPaysCurrency);
}

function parseOrderChange(node) {

  function parseOrderStatus(node) {
    // Create an Offer
    if (node.diffType === 'CreatedNode') {
      return 'created';
    }

    // Partially consume an Offer
    if (node.diffType === 'ModifiedNode') {
      return 'open';
    }

    if (node.diffType === 'DeletedNode') {
      // A consumed order has previous fields
      if (node.previousFields.hasOwnProperty('TakerPays')) {
        return 'closed';
      }

      // A canceled order has no previous fields
      return 'canceled';
    }
  }

  function parseChangeAmount(node, type) {
    var changeAmount;
    var status = parseOrderStatus(node);

    if (status === 'canceled') {
      // Canceled orders do not have PreviousFields and FinalFields
      // have positive values
      changeAmount = utils.parseCurrencyAmount(node.finalFields[type]);
      changeAmount.value = '0';
    } else if (status === 'created') {
      changeAmount = utils.parseCurrencyAmount(node.newFields[type]);
    } else {
      var finalAmount;
      changeAmount = finalAmount = utils.parseCurrencyAmount(
        node.finalFields[type]);

      if (node.previousFields[type]) {
        var previousAmount = utils.parseCurrencyAmount(
          node.previousFields[type]);
        var finalValue = new BigNumber(finalAmount.value);
        var prevValue = previousAmount ?
          new BigNumber(previousAmount.value) : 0;
        changeAmount.value = finalValue.minus(prevValue).toString();
      } else {
        // There is no previousField -- change must be zero
        changeAmount.value = '0';
      }
    }

    return changeAmount;
  }

  var orderChange = convertOrderChange({
    taker_pays: parseChangeAmount(node, 'TakerPays'),
    taker_gets: parseChangeAmount(node, 'TakerGets'),
    sell: (node.finalFields.Flags & lsfSell) !== 0,
    sequence: node.finalFields.Sequence || node.newFields.Sequence,
    status: parseOrderStatus(node),
    quality: getQuality(node)
  });

  Object.defineProperty(orderChange, 'account', {
    value: node.finalFields.Account || node.newFields.Account
  });

  return orderChange;
}

function groupByAddress(orderChanges) {
  return _.groupBy(orderChanges, function(change) {
    return change.account;
  });
}

/**
 * Computes the complete list of every Offer that changed in the ledger
 * as a result of the given transaction.
 * Returns changes grouped by Ripple account.
 *
 *  @param {Object} metadata - Transaction metadata as return by ripple-lib
 *  @returns {Object} - Orderbook changes grouped by Ripple account
 *
 */
exports.parseOrderbookChanges = function parseOrderbookChanges(metadata) {
  var nodes = utils.normalizeNodes(metadata);

  var orderChanges = _.map(_.filter(nodes, function(node) {
    return node.entryType === 'Offer';
  }), parseOrderChange);

  return groupByAddress(orderChanges);
};
