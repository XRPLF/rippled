
'use strict';
var _ = require('lodash');
var assert = require('assert');
var AccountFlags = require('./utils').constants.AccountFlags;
var parseFields = require('./fields');

function getAccountRootModifiedNode(tx) {
  var modifiedNodes = tx.meta.AffectedNodes.filter(function (node) {
    return node.ModifiedNode.LedgerEntryType === 'AccountRoot';
  });
  assert(modifiedNodes.length === 1);
  return modifiedNodes[0].ModifiedNode;
}

function parseFlags(tx) {
  var settings = {};
  if (tx.TransactionType !== 'AccountSet') {
    return settings;
  }

  var node = getAccountRootModifiedNode(tx);
  var oldFlags = _.get(node.PreviousFields, 'Flags');
  var newFlags = _.get(node.FinalFields, 'Flags');

  if (oldFlags !== undefined && newFlags !== undefined) {
    (function () {
      var changedFlags = oldFlags ^ newFlags;
      var setFlags = newFlags & changedFlags;
      var clearedFlags = oldFlags & changedFlags;
      _.forEach(AccountFlags, function (flagValue, flagName) {
        if (setFlags & flagValue) {
          settings[flagName] = true;
        } else if (clearedFlags & flagValue) {
          settings[flagName] = false;
        }
      });
    })();
  }

  // enableTransactionIDTracking requires a special case because it
  // does not affect the Flags field; instead it adds/removes a field called
  // "AccountTxnID" to/from the account root.

  var oldField = _.get(node.PreviousFields, 'AccountTxnID');
  var newField = _.get(node.FinalFields, 'AccountTxnID');
  if (newField && !oldField) {
    settings.enableTransactionIDTracking = true;
  } else if (oldField && !newField) {
    settings.enableTransactionIDTracking = false;
  }

  return settings;
}

function parseSettings(tx) {
  var txType = tx.TransactionType;
  assert(txType === 'AccountSet' || txType === 'SetRegularKey');

  var regularKey = tx.RegularKey ? { regularKey: tx.RegularKey } : {};
  return _.assign(regularKey, parseFlags(tx), parseFields(tx));
}

module.exports = parseSettings;