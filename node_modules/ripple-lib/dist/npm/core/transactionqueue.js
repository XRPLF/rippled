'use strict';

var lodash = require('lodash');
var LRU = require('lru-cache');
var Transaction = require('./transaction').Transaction;

/**
 * Manager for pending transactions
 */

function TransactionQueue() {
  this._queue = [];
  this._idCache = new LRU({ max: 200 });
  this._sequenceCache = new LRU({ max: 200 });
}

/**
 * Store received (validated) sequence
 *
 * @param {Number} sequence
 */

TransactionQueue.prototype.addReceivedSequence = function (sequence) {
  this._sequenceCache.set(String(sequence), true);
};

/**
 * Check that sequence number has been consumed by a validated
 * transaction
 *
 * @param {Number} sequence
 * @return {Boolean}
 */

TransactionQueue.prototype.hasSequence = function (sequence) {
  return this._sequenceCache.has(String(sequence));
};

/**
 * Store received (validated) ID transaction
 *
 * @param {String} transaction id
 * @param {Transaction} transaction
 */

TransactionQueue.prototype.addReceivedId = function (id, transaction) {
  this._idCache.set(id, transaction);
};

/**
 * Get received (validated) transaction by ID
 *
 * @param {String} transaction id
 * @return {Object}
 */

TransactionQueue.prototype.getReceived = function (id) {
  return this._idCache.get(id);
};

/**
 * Get a submitted transaction by ID. Transactions
 * may have multiple associated IDs.
 *
 * @param {String} transaction id
 * @return {Transaction}
 */

TransactionQueue.prototype.getSubmission = function (id) {
  return lodash.find(this._queue, function (tx) {
    return lodash.contains(tx.submittedIDs, id);
  });
};

/**
 * Get earliest ledger in the pending queue
 *
 * @return {Number} ledger
 */

TransactionQueue.prototype.getMinLedger = function () {
  if (this.length() < 1) {
    return -1;
  }

  var result = Infinity;

  for (var i = 0; i < this.length(); i++) {
    if (this._queue[i].initialSubmitIndex < result) {
      result = this._queue[i].initialSubmitIndex;
    }
  }

  if (!isFinite(result)) {
    result = -1;
  }

  return result;
};

/**
 * Remove a transaction from the queue
 *
 * @param {String|Transaction} transaction or id
 */

TransactionQueue.prototype.remove = function (tx) {
  // ND: We are just removing the Transaction by identity
  var i = this._queue.length;

  if (typeof tx === 'string') {
    tx = this.getSubmission(tx);
  }

  if (!(tx instanceof Transaction)) {
    return;
  }

  while (i--) {
    if (this._queue[i] === tx) {
      this._queue.splice(i, 1);
      break;
    }
  }
};

/**
 * Add a transaction to pending queue
 *
 * @param {Transaction} transaction
 */

TransactionQueue.prototype.push = function (tx) {
  this._queue.push(tx);
};

/**
 * Iterate over pending transactions
 *
 * @param {Function} iterator
 */

TransactionQueue.prototype.forEach = function (fn) {
  this._queue.forEach(fn);
};

/**
 * @return {Number} length of pending queue
 */

TransactionQueue.prototype.length = TransactionQueue.prototype.getLength = function () {
  return this._queue.length;
};

/**
 * @return {Array} pending queue
 */

TransactionQueue.prototype.getQueue = function () {
  return this._queue;
};

exports.TransactionQueue = TransactionQueue;