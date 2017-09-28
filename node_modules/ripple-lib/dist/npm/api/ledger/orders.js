
'use strict';
var _ = require('lodash');
var utils = require('./utils');
var validate = utils.common.validate;
var composeAsync = utils.common.composeAsync;
var convertErrors = utils.common.convertErrors;
var parseAccountOrder = require('./parse/account-order');

function requestAccountOffers(remote, address, ledgerVersion, marker, limit, callback) {
  remote.requestAccountOffers({
    account: address,
    marker: marker,
    limit: utils.clamp(limit, 10, 400),
    ledger: ledgerVersion
  }, composeAsync(function (data) {
    return {
      marker: data.marker,
      results: data.offers.map(_.partial(parseAccountOrder, address))
    };
  }, convertErrors(callback)));
}

function getOrdersAsync(account, options, callback) {
  validate.address(account);
  validate.getOrdersOptions(options);

  var ledgerVersion = options.ledgerVersion || this.remote.getLedgerSequence();
  var getter = _.partial(requestAccountOffers, this.remote, account, ledgerVersion);
  utils.getRecursive(getter, options.limit, composeAsync(function (orders) {
    return _.sortBy(orders, function (order) {
      return order.properties.sequence;
    });
  }, callback));
}

function getOrders(account) {
  var options = arguments.length <= 1 || arguments[1] === undefined ? {} : arguments[1];

  return utils.promisify(getOrdersAsync).call(this, account, options);
}

module.exports = getOrders;