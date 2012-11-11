// Routines for working with an account.
//
// Events:
//   wallet_clean	: True, iff the wallet has been updated.
//   wallet_dirty	: True, iff the wallet needs to be updated.
//   balance		: The current stamp balance.
//   balance_proposed
//

// var network = require("./network.js");

var EventEmitter  = require('events').EventEmitter;
var Amount	  = require('./amount.js').Amount;
var UInt160	  = require('./amount.js').UInt160;

var Account = function (network, account) {
    this._network   = network;
    this._account   = UInt160.json_rewrite(account);

    return this;
};

exports.Account	    = Account;

// vim:sw=2:sts=2:ts=8
