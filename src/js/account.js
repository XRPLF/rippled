// Routines for working with an account.
//
// Events:
//   wallet_clean	: True, iff the wallet has been updated.
//   wallet_dirty	: True, iff the wallet needs to be updated.
//   balance		: The current stamp balance.
//   balance_proposed
//

// var network = require("./network.js");

var EventEmitter = require('events').EventEmitter;
var Amount = require('./amount').Amount;
var UInt160 = require('./uint160').UInt160;

var Account = function (remote, account) {
  var self = this;

  this._remote = remote;
  this._account = UInt160.from_json(account);

  // Ledger entry object
  // Important: This must never be overwritten, only extend()-ed
  this._entry = {};

  this.on('newListener', function (type, listener) {
    if (Account.subscribe_events.indexOf(type) !== -1) {
      if (!this._subs && 'open' === this._remote._online_state) {
        this._remote.request_subscribe()
          .accounts(this._account.to_json())
          .request();
      }
      this._subs  += 1;
    }
  });

  this.on('removeListener', function (type, listener) {
    if (Account.subscribe_events.indexOf(type) !== -1) {
      this._subs  -= 1;

      if (!this._subs && 'open' === this._remote._online_state) {
        this._remote.request_unsubscribe([ 'transactions' ])
          .accounts(this._account.to_json())
          .request();
      }
    }
  });

  this._remote.on('connect', function () {
    if (self._subs) {
      this._remote.request_subscribe()
        .accounts(this._account.to_json())
        .request();
    }
  });

  return this;
};

Account.prototype = new EventEmitter;

/**
 * List of events that require a remote subscription to the account.
 */
Account.subscribe_events = ['transaction', 'entry'];

Account.prototype.to_json = function ()
{
  return this._account.to_json();
};

/**
 * Whether the AccountId is valid.
 *
 * Note: This does not tell you whether the account exists in the ledger.
 */
Account.prototype.is_valid = function ()
{
  return this._account.is_valid();
};

exports.Account	    = Account;

// vim:sw=2:sts=2:ts=8:et
