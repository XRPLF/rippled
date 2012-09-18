// Work with transactions.
//
// This works for both trusted and untrusted servers.
//
// For untrusted servers:
//    - We never send a secret to an untrusted server.
//    - Convert transactions to and from JSON.
//    - Sign and verify signatures.
//    - Encrypt and decrypt.
//
// For trusted servers:
//    - We need a websocket way of working with transactions as JSON.
//    - This allows us to not need to port the transaction tools to so many
//      languages.
//

var commands = {};

commands.buildSend = function(params) {
	var	srcAccountID	= params.srcAccountID;
	var fee				= params.fee;
	var	dstAccountID	= params.dstAccountID;
	var	amount			= params.amount;
	var	sendMax			= params.sendMax;
	var	partial			= params.partial;
	var limit			= params.limit;
};


exports.trustedCreate = function() {

};

exports.untrustedCreate = function() {

};

// vim:ts=4
