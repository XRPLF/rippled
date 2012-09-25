// Remote access to a server.
// - We never send binary data.
// - We use the W3C interface for node and browser compatibility:
//   http://www.w3.org/TR/websockets/#the-websocket-interface
//
// YYY Will later provide a network access which use multiple instances of this.
//

var util = require('util');

var WebSocket = require('ws');

// YYY This is wrong should not use anything in test directory.
var config = require("../test/config.js");

// --> trusted: truthy, if remote is trusted
var Remote = function(trusted, websocket_ip, websocket_port) {
	this.trusted		= trusted;
	this.websocket_ip	= websocket_ip;
	this.websocket_port	= websocket_port;
	this.id				= 0;
};

var remoteConfig = function(server) {
	var	serverConfig	= config.servers[server];

	return new Remote(serverConfig.trusted, serverConfig.websocket_ip, serverConfig.websocket_port);
};

// Target state is connectted.
// done(readyState):
// --> readyState: OPEN, CLOSED
Remote.method('connect', function(done, onmessage) {
	var	url	= util.format("ws://%s:%s", this.websocket_ip, this.websocket_port);

	console.log("remote: connect: %s", url);

	this.ws	= new WebSocket(url);

	var ws = this.ws;

	ws.onopen		= function() {
		console.log("remote: onopen: %s", ws.readyState);
		ws.onclose	= undefined;
		done(ws.readyState);
		};

	// Also covers failure to open.
	ws.onclose		= function() {
		console.log("remote: onclose: %s", ws.readyState);
		done(ws.readyState);
		};

	if (onmessage) {
		ws.onmessage	= onmessage;
	}
});

// Target stated is disconnected.
Remote.method('disconnect', function(done) {
	var ws = this.ws;

	ws.onclose		= function() {
		console.log("remote: onclose: %s", ws.readyState);
		done(ws.readyState);
		};

	ws.close();
});

// Send a command. The comman should lack the id.
// <-> command: what to send, consumed.
Remote.method('request', function(command, done) {
	this.id += 1;	// Advance id.

	var ws = this.ws;

	command.id	= this.id;

	ws.response[command.id] = done;

	ws.send(command);
});

// Request the current ledger.
// done(index)
// index: undefined = error
Remote.method('ledger', function(done) {

});


// Submit a json transaction.
// done(value)
// <-> value: { 'status', status, 'result' : result, ... }
// done may be called up to 3 times.
Remote.method('submit', function(json, done) {
//	this.request(..., function() {
//		});
});

// ==> entry_spec
Remote.method('ledger_entry', function(entry_spec, done) {
	entry_spec.command	= 'ledger_entry';

	this.request(entry_spec, function() {
		});
});

// done(value)
// --> value: { 'status', status, 'result' : result, ... }
// done may be called up to 3 times.
Remote.method('account_root', function(account_id, done) {
	this.request({
			'command' : 'ledger_entry',
		}, function() {
		});
});

exports.Remote = Remote;
exports.remoteConfig = remoteConfig;

// vim:ts=4
