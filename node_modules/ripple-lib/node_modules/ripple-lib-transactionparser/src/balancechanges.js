var _ = require('lodash');
var BigNumber = require('bignumber.js');
var normalizeNodes = require('./utils').normalizeNodes;
var dropsToXRP = require('./utils').dropsToXRP;

function parseBalance(balance) {
  return new BigNumber(balance.value || balance);
}

function computeBalanceChange(node) {
  if (node.newFields.Balance) {
    return parseBalance(node.newFields.Balance)
  } else if(node.previousFields.Balance && node.finalFields.Balance) {
    return parseBalance(node.finalFields.Balance).minus(
      parseBalance(node.previousFields.Balance));
  } else {
    return new BigNumber(0);
  }
}

function parseXRPBalanceChange(node) {
  var balanceChange = computeBalanceChange(node);

  if(balanceChange.isZero()) {
    return null;
  }

  return {
    address: node.finalFields.Account || node.newFields.Account,
    balance_change: {
      counterparty: '',
      currency: 'XRP',
      value: dropsToXRP(balanceChange).toString()
    }
  };
}

function flipBalanceChange(change) {
  var negatedBalance = (new BigNumber(change.balance_change.value)).negated();
  return {
    address: change.balance_change.counterparty,
    balance_change: {
      counterparty: change.address,
      currency: change.balance_change.currency,
      value: negatedBalance.toString()
    }
  };
}

function parseTrustlineBalanceChanges(node) {
  var balanceChange = computeBalanceChange(node);

  if(balanceChange.isZero()) {
    return null;
  }

  /*
   * A trustline can be created with a non-zero starting balance
   * If an offer is placed to acquire an asset with no existing trustline,
   * the trustline can be created when the ofer is taken.
   */
  var fields = _.isEmpty(node.newFields) ? node.finalFields : node.newFields;

  // the balance is always from low node's perspective
  var change = {
    address: fields.LowLimit.issuer,
    balance_change: {
      counterparty: fields.HighLimit.issuer,
      currency: fields.Balance.currency,
      value: balanceChange.toString()
    }
  };
  return [change, flipBalanceChange(change)];
}

function groupByAddress(balanceChanges) {
  var grouped = _.groupBy(balanceChanges, function(change) {
    return change.address;
  });
  return _.mapValues(grouped, function(group) {
    return _.map(group, function(change) {
      return change.balance_change;
    });
  });
}

/**
 * Computes the complete list of every balance that changed in the ledger
 * as a result of the given transaction.
 */
function parseBalanceChanges(metadata) {
  var balanceChanges = normalizeNodes(metadata).map(function(node) {
    if (node.entryType === 'AccountRoot') {
      return [parseXRPBalanceChange(node)];
    } else if (node.entryType === 'RippleState') {
      return parseTrustlineBalanceChanges(node);
    } else {
      return [ ];
    }
  });
  return groupByAddress(_.compact(_.flatten(balanceChanges)));
}


module.exports.parseBalanceChanges = parseBalanceChanges;
