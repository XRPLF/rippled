
'use strict';
var _ = require('lodash');
var utils = require('./utils');
var validate = utils.common.validate;
var parseFields = require('./parse/fields');
var composeAsync = utils.common.composeAsync;
var AccountFlags = utils.common.constants.AccountFlags;
var convertErrors = utils.common.convertErrors;

function parseFlags(value) {
  var settings = {};
  for (var flagName in AccountFlags) {
    if (value & AccountFlags[flagName]) {
      settings[flagName] = true;
    }
  }
  return settings;
}

function formatSettings(response) {
  var data = response.account_data;
  var parsedFlags = parseFlags(data.Flags);
  var parsedFields = parseFields(data);
  return _.assign({}, parsedFlags, parsedFields);
}

function getSettingsAsync(account, options, callback) {
  validate.address(account);
  validate.getSettingsOptions(options);

  var request = {
    account: account,
    ledger: options.ledgerVersion || 'validated'
  };

  this.remote.requestAccountInfo(request, composeAsync(formatSettings, convertErrors(callback)));
}

function getSettings(account) {
  var options = arguments.length <= 1 || arguments[1] === undefined ? {} : arguments[1];

  return utils.promisify(getSettingsAsync).call(this, account, options);
}

module.exports = getSettings;