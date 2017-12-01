'use strict';
var core = require('./utils').core;
var flagIndices = core.Transaction.set_clear_flags.AccountSet;
var flags = core.Remote.flags.account_root;

var AccountFlags = {
  passwordSpent: flags.PasswordSpent,
  requireDestinationTag: flags.RequireDestTag,
  requireAuthorization: flags.RequireAuth,
  disallowIncomingXRP: flags.DisallowXRP,
  disableMasterKey: flags.DisableMaster,
  noFreeze: flags.NoFreeze,
  globalFreeze: flags.GlobalFreeze,
  defaultRipple: flags.DefaultRipple
};

var AccountFlagIndices = {
  requireDestinationTag: flagIndices.asfRequireDest,
  requireAuthorization: flagIndices.asfRequireAuth,
  disallowIncomingXRP: flagIndices.asfDisallowXRP,
  disableMasterKey: flagIndices.asfDisableMaster,
  enableTransactionIDTracking: flagIndices.asfAccountTxnID,
  noFreeze: flagIndices.asfNoFreeze,
  globalFreeze: flagIndices.asfGlobalFreeze,
  defaultRipple: flagIndices.asfDefaultRipple
};

var AccountFields = {
  EmailHash: { name: 'emailHash', encoding: 'hex',
    length: 32, defaults: '0' },
  WalletLocator: { name: 'walletLocator', encoding: 'hex',
    length: 64, defaults: '0' },
  WalletSize: { name: 'walletSize', defaults: 0 },
  MessageKey: { name: 'messageKey' },
  Domain: { name: 'domain', encoding: 'hex' },
  TransferRate: { name: 'transferRate', defaults: 0, shift: 9 },
  Signers: { name: 'signers' }
};

module.exports = {
  AccountFields: AccountFields,
  AccountFlagIndices: AccountFlagIndices,
  AccountFlags: AccountFlags
};