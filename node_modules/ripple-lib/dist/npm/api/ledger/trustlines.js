
'use strict';
var _ = require('lodash');
var utils = require('./utils');
var validate = utils.common.validate;
var composeAsync = utils.common.composeAsync;
var convertErrors = utils.common.convertErrors;
var parseAccountTrustline = require('./parse/account-trustline');

function currencyFilter(currency, trustline) {
  return currency === null || trustline.specification.currency === currency;
}

function formatResponse(options, data) {
  return {
    marker: data.marker,
    results: data.lines.map(parseAccountTrustline).filter(_.partial(currencyFilter, options.currency || null))
  };
}

function getAccountLines(remote, address, ledgerVersion, options, marker, limit, callback) {
  var requestOptions = {
    account: address,
    ledger: ledgerVersion,
    marker: marker,
    limit: utils.clamp(limit, 10, 400),
    peer: options.counterparty
  };

  remote.requestAccountLines(requestOptions, composeAsync(_.partial(formatResponse, options), convertErrors(callback)));
}

function getTrustlinesAsync(account, options, callback) {
  validate.address(account);
  validate.getTrustlinesOptions(options);

  var ledgerVersion = options.ledgerVersion || this.remote.getLedgerSequence();
  var getter = _.partial(getAccountLines, this.remote, account, ledgerVersion, options);
  utils.getRecursive(getter, options.limit, callback);
}

function getTrustlines(account) {
  var options = arguments.length <= 1 || arguments[1] === undefined ? {} : arguments[1];

  return utils.promisify(getTrustlinesAsync).call(this, account, options);
}

module.exports = getTrustlines;