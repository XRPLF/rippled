// Routines for working with an orderbook.
//
// One OrderBook object represents one half of an order book. (i.e. bids OR
// asks) Which one depends on the ordering of the parameters.
//
// Events:
//  - transaction   A transaction that affects the order book.

// var network = require("./network.js");

var EventEmitter = require('events').EventEmitter;
var Amount = require('./amount').Amount;
var UInt160 = require('./uint160').UInt160;
var Currency = require('./currency').Currency;

var extend = require('extend');

var OrderBook = function (remote,
                          currency_gets, issuer_gets,
                          currency_pays, issuer_pays) {
  var self = this;

  this._remote = remote;
  this._currency_gets = currency_gets;
  this._issuer_gets = issuer_gets;
  this._currency_pays = currency_pays;
  this._issuer_pays = issuer_pays;

  this._subs = 0;

  // We consider ourselves synchronized if we have a current copy of the offers,
  // we are online and subscribed to updates.
  this._sync = false;

  // Offers
  this._offers = [];

  this.on('newListener', function (type, listener) {
    if (OrderBook.subscribe_events.indexOf(type) !== -1) {
      if (!self._subs && 'open' === self._remote._online_state) {
        self._subscribe();
      }
      self._subs  += 1;
    }
  });

  this.on('removeListener', function (type, listener) {
    if (OrderBook.subscribe_events.indexOf(type) !== -1) {
      self._subs  -= 1;

      if (!self._subs && 'open' === self._remote._online_state) {
        self._sync = false;
        self._remote.request_unsubscribe()
          .books([self.to_json()])
          .request();
      }
    }
  });

  this._remote.on('connect', function () {
    if (self._subs) {
      self._subscribe();
    }
  });

  this._remote.on('disconnect', function () {
    self._sync = false;
  });

  return this;
};

OrderBook.prototype = new EventEmitter;

/**
 * List of events that require a remote subscription to the orderbook.
 */
OrderBook.subscribe_events = ['transaction', 'model', 'trade'];

/**
 * Subscribes to orderbook.
 *
 * @private
 */
OrderBook.prototype._subscribe = function ()
{
  var self = this;
  self._remote.request_subscribe()
    .books([self.to_json()], true)
    .on('error', function () {
      // XXX What now?
    })
    .on('success', function (res) {
      self._sync = true;
      self._offers = res.offers;
      self.emit('model', self._offers);
    })
    .request();
};

OrderBook.prototype.to_json = function ()
{
  var json = {
    "taker_gets": {
      "currency": this._currency_gets
    },
    "taker_pays": {
      "currency": this._currency_pays
    }
  };

  if (this._currency_gets !== "XRP") json["taker_gets"]["issuer"] = this._issuer_gets;
  if (this._currency_pays !== "XRP") json["taker_pays"]["issuer"] = this._issuer_pays;

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
  // XXX Should check for same currency (non-native) && same issuer
  return (
    Currency.is_valid(this._currency_pays) &&
    (this._currency_pays === "XRP" || UInt160.is_valid(this._issuer_pays)) &&
    Currency.is_valid(this._currency_gets) &&
    (this._currency_gets === "XRP" || UInt160.is_valid(this._issuer_gets)) &&
    !(this._currency_pays === "XRP" && this._currency_gets === "XRP")
  );
};

/**
 * Notify object of a relevant transaction.
 *
 * This is only meant to be called by the Remote class. You should never have to
 * call this yourself.
 */
OrderBook.prototype.notifyTx = function (message)
{
  var self = this;

  var changed = false;

  var trade_gets = Amount.from_json("0" + ((this._currency_gets === 'XRP') ? "" :
                                    "/" + this._currency_gets +
                                    "/" + this._issuer_gets));
  var trade_pays = Amount.from_json("0" + ((this._currency_pays === 'XRP') ? "" :
                                    "/" + this._currency_pays +
                                    "/" + this._issuer_pays));

  message.mmeta.each(function (an) {
    if (an.entryType !== 'Offer') return;

    var i, l, offer;
    if (an.diffType === 'DeletedNode' ||
        an.diffType === 'ModifiedNode') {
      for (i = 0, l = self._offers.length; i < l; i++) {
        offer = self._offers[i];
        if (offer.index === an.ledgerIndex) {
          if (an.diffType === 'DeletedNode') {
            self._offers.splice(i, 1);
          }
          else extend(offer, an.fieldsFinal);
          changed = true;
          break;
        }
      }

      // We don't want to count a OfferCancel as a trade
      if (message.transaction.TransactionType === "OfferCancel") return;

      trade_gets = trade_gets.add(an.fieldsPrev.TakerGets);
      trade_pays = trade_pays.add(an.fieldsPrev.TakerPays);
      if (an.diffType === 'ModifiedNode') {
        trade_gets = trade_gets.subtract(an.fieldsFinal.TakerGets);
        trade_pays = trade_pays.subtract(an.fieldsFinal.TakerPays);
      }
    } else if (an.diffType === 'CreatedNode') {
      var price = Amount.from_json(an.fields.TakerPays).ratio_human(an.fields.TakerGets);
      for (i = 0, l = self._offers.length; i < l; i++) {
        offer = self._offers[i];
        var priceItem = Amount.from_json(offer.TakerPays).ratio_human(offer.TakerGets);

        if (price.compareTo(priceItem) <= 0) {
          var obj = an.fields;
          obj.index = an.ledgerIndex;
          self._offers.splice(i, 0, an.fields);
          changed = true;
          break;
        }
      }
    }
  });

  // Only trigger the event if the account object is actually
  // subscribed - this prevents some weird phantom events from
  // occurring.
  if (this._subs) {
    this.emit('transaction', message);
    if (changed) this.emit('model', this._offers);
    if (!trade_gets.is_zero()) this.emit('trade', trade_pays, trade_gets);
  }
};

/**
 * Get offers model asynchronously.
 *
 * This function takes a callback and calls it with an array containing the
 * current set of offers in this order book.
 *
 * If the data is available immediately, the callback may be called synchronously.
 */
OrderBook.prototype.offers = function (callback)
{
  var self = this;

  if ("function" === typeof callback) {
    if (this._sync) {
      callback(this._offers);
    } else {
      this.once('model', function (offers) {
        callback(offers);
      });
    }
  }
  return this;
};

/**
 * Return latest known offers.
 *
 * Usually, this will just be an empty array if the order book hasn't been
 * loaded yet. But this accessor may be convenient in some circumstances.
 */
OrderBook.prototype.offersSync = function ()
{
  return this._offers;
};

exports.OrderBook	    = OrderBook;

// vim:sw=2:sts=2:ts=8:et
