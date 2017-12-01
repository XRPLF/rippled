

'use strict';

var _Promise = require('babel-runtime/core-js/promise')['default'];

var utils = require('./utils');
var removeUndefined = require('./parse/utils').removeUndefined;
var validate = utils.common.validate;
var composeAsync = utils.common.composeAsync;
var convertErrors = utils.common.convertErrors;

function formatAccountInfo(response) {
  var data = response.account_data;
  return removeUndefined({
    sequence: data.Sequence,
    xrpBalance: utils.common.dropsToXrp(data.Balance),
    ownerCount: data.OwnerCount,
    previousInitiatedTransactionID: data.AccountTxnID,
    previousAffectingTransactionID: data.PreviousTxnID,
    previousAffectingTransactionLedgerVersion: data.PreviousTxnLgrSeq
  });
}

function getAccountInfoAsync(account, options, callback) {
  validate.address(account);
  validate.getAccountInfoOptions(options);

  var request = {
    account: account,
    ledger: options.ledgerVersion || 'validated'
  };

  this.remote.requestAccountInfo(request, composeAsync(formatAccountInfo, convertErrors(callback)));
}

function getAccountInfo(account) {
  var options = arguments.length <= 1 || arguments[1] === undefined ? {} : arguments[1];

  return utils.promisify(getAccountInfoAsync).call(this, account, options);
}

module.exports = getAccountInfo;