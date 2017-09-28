
'use strict';
var utils = require('./utils');
var validate = utils.common.validate;
var composeAsync = utils.common.composeAsync;
var convertErrors = utils.common.convertErrors;
var parseLedger = require('./parse/ledger');

function getLedgerAsync(options, callback) {
  validate.getLedgerOptions(options);

  var request = {
    ledger: options.ledgerVersion || 'validated',
    expand: options.includeAllData,
    transactions: options.includeTransactions,
    accounts: options.includeState
  };

  this.remote.requestLedger(request, composeAsync(function (response) {
    return parseLedger(response.ledger);
  }, convertErrors(callback)));
}

function getLedger() {
  var options = arguments.length <= 0 || arguments[0] === undefined ? {} : arguments[0];

  return utils.promisify(getLedgerAsync).call(this, options);
}

module.exports = getLedger;