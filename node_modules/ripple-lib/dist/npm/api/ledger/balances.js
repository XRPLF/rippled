
'use strict';
var _ = require('lodash');
var async = require('async');
var utils = require('./utils');
var getTrustlines = require('./trustlines');
var validate = utils.common.validate;
var composeAsync = utils.common.composeAsync;
var convertErrors = utils.common.convertErrors;

function getTrustlineBalanceAmount(trustline) {
  return {
    currency: trustline.specification.currency,
    counterparty: trustline.specification.counterparty,
    value: trustline.state.balance
  };
}

function formatBalances(balances) {
  var xrpBalance = {
    currency: 'XRP',
    value: balances.xrp
  };
  return [xrpBalance].concat(balances.trustlines.map(getTrustlineBalanceAmount));
}

function getTrustlinesAsync(account, options, callback) {
  getTrustlines.call(this, account, options).then(function (data) {
    return callback(null, data);
  })['catch'](callback);
}

function getBalancesAsync(account, options, callback) {
  validate.address(account);
  validate.getBalancesOptions(options);

  var ledgerVersion = options.ledgerVersion || this.remote.getLedgerSequence();
  async.parallel({
    xrp: _.partial(utils.getXRPBalance, this.remote, account, ledgerVersion),
    trustlines: _.partial(getTrustlinesAsync.bind(this), account, options)
  }, composeAsync(formatBalances, convertErrors(callback)));
}

function getBalances(account) {
  var options = arguments.length <= 1 || arguments[1] === undefined ? {} : arguments[1];

  return utils.promisify(getBalancesAsync).call(this, account, options);
}

module.exports = getBalances;