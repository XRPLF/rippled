
'use strict';
var _ = require('lodash');
var removeUndefined = require('./utils').removeUndefined;
var parseTransaction = require('./transaction');

function parseTransactions(transactions) {
  if (_.isEmpty(transactions)) {
    return {};
  }
  if (_.isString(transactions[0])) {
    return { transactionHashes: transactions };
  }
  return {
    transactions: _.map(transactions, parseTransaction),
    rawTransactions: JSON.stringify(transactions)
  };
}

function parseState(state) {
  if (_.isEmpty(state)) {
    return {};
  }
  if (_.isString(state[0])) {
    return { stateHashes: state };
  }
  return { rawState: JSON.stringify(state) };
}

function parseLedger(ledger) {
  return removeUndefined(_.assign({
    accepted: ledger.accepted,
    closed: ledger.closed,
    stateHash: ledger.account_hash,
    closeTime: ledger.close_time,
    closeTimeResolution: ledger.close_time_resolution,
    closeFlags: ledger.close_flags,
    ledgerHash: ledger.hash || ledger.ledger_hash,
    ledgerVersion: parseInt(ledger.ledger_index || ledger.seqNum, 10),
    parentLedgerHash: ledger.parent_hash,
    parentCloseTime: ledger.parent_close_time,
    totalDrops: ledger.total_coins || ledger.totalCoins,
    transactionHash: ledger.transaction_hash
  }, parseTransactions(ledger.transactions), parseState(ledger.accountState)));
}

module.exports = parseLedger;