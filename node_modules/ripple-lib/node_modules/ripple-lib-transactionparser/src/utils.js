'use strict';
var _ = require('lodash');
var BigNumber = require('bignumber.js');

// drops is a bignumber.js BigNumber
function dropsToXRP(drops) {
  return drops.dividedBy(1000000);
}

function normalizeNode(affectedNode) {
  var diffType = Object.keys(affectedNode)[0];
  var node = affectedNode[diffType];
  return {
    diffType: diffType,
    entryType: node.LedgerEntryType,
    ledgerIndex: node.LedgerIndex,
    newFields: node.NewFields || {},
    finalFields: node.FinalFields || {},
    previousFields: node.PreviousFields || {}
  };
}

function normalizeNodes(metadata) {
  if (!metadata.AffectedNodes) {
    return [];
  }
  return metadata.AffectedNodes.map(normalizeNode);
}

function parseCurrencyAmount(currencyAmount) {
  if (typeof currencyAmount === 'string') {
    return {
      currency: 'XRP',
      value: dropsToXRP(new BigNumber(currencyAmount)).toString()
    };
  }

  return {
    currency: currencyAmount.currency,
    counterparty: currencyAmount.issuer,
    value: currencyAmount.value
  };
}

function isAccountField(fieldName) {
  var fieldNames = ['Account', 'Owner', 'Destination', 'Issuer', 'Target'];
  return _.includes(fieldNames, fieldName);
}

function isAmountFieldAffectingIssuer(fieldName) {
  var fieldNames = ['LowLimit', 'HighLimit', 'TakerPays', 'TakerGets'];
  return _.includes(fieldNames, fieldName);
}

function getAffectedAccounts(metadata) {
  var accounts = [];
  _.forEach(normalizeNodes(metadata), function(node) {
    var fields = node.diffType === 'CreatedNode' ?
      node.newFields : node.finalFields;
    _.forEach(fields, function(fieldValue, fieldName) {
      if (isAccountField(fieldName)) {
        accounts.push(fieldValue);
      } else if (isAmountFieldAffectingIssuer(fieldName) && fieldValue.issuer) {
        accounts.push(fieldValue.issuer);
      }
    });
  });
  return _.uniq(accounts);
}

module.exports = {
  dropsToXRP: dropsToXRP,
  normalizeNodes: normalizeNodes,
  parseCurrencyAmount: parseCurrencyAmount,
  getAffectedAccounts: getAffectedAccounts
};
