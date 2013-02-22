// Routines for working with an orderbook.
//
// Events:

// var network = require("./network.js");

var EventEmitter = require('events').EventEmitter;
var Amount = require('./amount').Amount;
var UInt160 = require('./uint160').UInt160;
var Currency = require('./currency').Currency;

var extend = require('extend');

var OrderBook = function (remote,
                          currency_out, issuer_out,
                          currency_in,  issuer_in) {
  var self = this;

  this._remote = remote;
  this._currency_out = currency_out;
  this._issuer_out = issuer_out;
  this._currency_in = currency_in;
  this._issuer_in = issuer_in;

  this._subs = 0;

  // Ledger entry object
  // Important: This must never be overwritten, only extend()-ed
  this._entry = {};

  this.on('newListener', function (type, listener) {
    if (OrderBook.subscribe_events.indexOf(type) !== -1) {
      if (!self._subs && 'open' === self._remote._online_state) {
        self._remote.request_subscribe()
          .books([self.to_json()])
          .request();
      }
      self._subs  += 1;
    }
  });

  this.on('removeListener', function (type, listener) {
    if (OrderBook.subscribe_events.indexOf(type) !== -1) {
      self._subs  -= 1;

      if (!self._subs && 'open' === self._remote._online_state) {
        self._remote.request_unsubscribe()
          .books([self.to_json()])
          .request();
      }
    }
  });

  this._remote.on('connect', function () {
    if (self._subs) {
      self._remote.request_subscribe()
        .books([self.to_json()])
        .request();
    }
  });

  return this;
};

OrderBook.prototype = new EventEmitter;

/**
 * List of events that require a remote subscription to the orderbook.
 */
OrderBook.subscribe_events = ['transaction'];

OrderBook.prototype.to_json = function ()
{
  var json = {
    "CurrencyOut": this._currency_out,
    "CurrencyIn": this._currency_in
  };

  if (json["CurrencyOut"] !== "XRP") json["IssuerOut"] = this._issuer_out;
  if (json["CurrencyIn"] !== "XRP")  json["IssuerIn"]  = this._issuer_in;

  return json;
};

/**
 * Whether the OrderBook is valid.
 *
 * Note: This only checks whether the parameters (currencies and issuer) are
 *       syntactically valid. It does not check anything against the ledger.
 */
OrderBook.prototype.is_valid = function ()
{
  return (
    Currency.is_valid(this._currency_in) &&
    (this._currency_in !== "XRP" && UInt160.is_valid(this._issuer_in)) &&
    Currency.is_valid(this._currency_out) &&
    (this._currency_out !== "XRP" && UInt160.is_valid(this._issuer_out)) &&
    !(this._currency_in === "XRP" && this._currency_out === "XRP")
  );
};

exports.OrderBook	    = OrderBook;

// vim:sw=2:sts=2:ts=8:et
