
'use strict';
var _ = require('lodash');
var common = require('../common');

function convertLedgerHeader(header) {
  return {
    accepted: header.accepted,
    closed: header.closed,
    account_hash: header.stateHash,
    close_time: header.closeTime,
    close_time_resolution: header.closeTimeResolution,
    close_flags: header.closeFlags,
    hash: header.ledgerHash,
    ledger_hash: header.ledgerHash,
    ledger_index: header.ledgerVersion.toString(),
    seqNum: header.ledgerVersion.toString(),
    parent_hash: header.parentLedgerHash,
    parent_close_time: header.parentCloseTime,
    total_coins: header.totalDrops,
    totalCoins: header.totalDrops,
    transaction_hash: header.transactionHash
  };
}

function hashLedgerHeader(ledgerHeader) {
  var header = convertLedgerHeader(ledgerHeader);
  return common.core.Ledger.calculateLedgerHash(header);
}

function computeTransactionHash(ledger) {
  if (ledger.rawTransactions === undefined) {
    return ledger.transactionHash;
  }
  var transactions = JSON.parse(ledger.rawTransactions);
  var txs = _.map(transactions, function (tx) {
    var mergeTx = _.assign({}, _.omit(tx, 'tx'), tx.tx || {});
    var renameMeta = _.assign({}, _.omit(mergeTx, 'meta'), tx.meta ? { metaData: tx.meta } : {});
    return renameMeta;
  });
  var ledgerObject = common.core.Ledger.from_json({ transactions: txs });
  var transactionHash = ledgerObject.calc_tx_hash().to_hex();
  if (ledger.transactionHash !== undefined && ledger.transactionHash !== transactionHash) {
    throw new common.errors.ValidationError('transactionHash in header' + ' does not match computed hash of transactions');
  }
  return transactionHash;
}

function computeStateHash(ledger) {
  if (ledger.rawState === undefined) {
    return ledger.stateHash;
  }
  var state = JSON.parse(ledger.rawState);
  var ledgerObject = common.core.Ledger.from_json({ accountState: state });
  var stateHash = ledgerObject.calc_account_hash().to_hex();
  if (ledger.stateHash !== undefined && ledger.stateHash !== stateHash) {
    throw new common.errors.ValidationError('stateHash in header' + ' does not match computed hash of state');
  }
  return stateHash;
}

function computeLedgerHash(ledger) {
  var hashes = {
    transactionHash: computeTransactionHash(ledger),
    stateHash: computeStateHash(ledger)
  };
  return hashLedgerHeader(_.assign({}, ledger, hashes));
}

module.exports = computeLedgerHash;