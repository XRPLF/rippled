// Remote access to a server.
// - We never send binary data.
// - We use the W3C interface for node and browser compatibility:
//   http://www.w3.org/TR/websockets/#the-websocket-interface
//
// YYY Will later provide a network access which use multiple instances of this.
// YYY A better model might be to allow requesting a target state: keep connected or not.
//

var util = require('util');

var WebSocket = require('ws');

// --> trusted: truthy, if remote is trusted
var Remote = function(trusted, websocket_ip, websocket_port, trace) {
	this.trusted		= trusted;
	this.websocket_ip	= websocket_ip;
	this.websocket_port	= websocket_port;
	this.id				= 0;
	this.trace			= trace;
};

var remoteConfig = function(config, server, trace) {
	var	serverConfig	= config.servers[server];

	return new Remote(serverConfig.trusted, serverConfig.websocket_ip, serverConfig.websocket_port, trace);
};

Remote.method('connect_helper', function() {
	var	self	= this;

	if (this.trace)
		console.log("remote: connect: %s", this.url);

	this.ws		= new WebSocket(this.url);

	var ws = this.ws;

	ws.response	= {};

	ws.onopen	= function() {
			if (this.trace)
				console.log("remote: onopen: %s", ws.readyState);

			ws.onclose	= undefined;
			ws.onerror	= undefined;

			self.done(ws.readyState);
		};

	ws.onerror	= function() {
			if (this.trace)
				console.log("remote: onerror: %s", ws.readyState);

			ws.onclose	= undefined;

			if (self.expire) {
				if (this.trace)
					console.log("remote: was expired");

				self.done(ws.readyState);
			}
			else
			{
				// Delay and retry.
				setTimeout(function() {
						if (this.trace)
							console.log("remote: retry");

						self.connect_helper();
					}, 50);	// Retry rate 50ms.
			}
		};

	// Covers failure to open.
	ws.onclose	= function() {
			if (this.trace)
				console.log("remote: onclose: %s", ws.readyState);

			ws.onerror	= undefined;

			self.done(ws.readyState);
		};

	// Node's ws module doesn't pass arguments to onmessage.
	ws.on('message', function(json, flags) {
		var	message	= JSON.parse(json);
		// console.log("message: %s", json);

		if (message.type !== 'response') {
			console.log("unexpected message: %s", json);

		} else {
			var done	= ws.response[message.id];

			if (done) {
				done(message);

			} else {
				console.log("unexpected message id: %s", json);
			}
		}
	});
});

// Target state is connectted.
// done(readyState):
// --> readyState: OPEN, CLOSED
Remote.method('connect', function(done, timeout) {
	var self	= this;

	this.url	= util.format("ws://%s:%s", this.websocket_ip, this.websocket_port);
	this.done	= done;

	if (timeout) {
		if (this.trace)
			console.log("remote: expire: false");

		this.expire	= false;

		setTimeout(function () {
				if (this.trace)
					console.log("remote: expire: timeout");

				self.expire	= true;
			}, timeout);
	}
	else {
		if (this.trace)
			console.log("remote: expire: false");

		this.expire	= true;
	}

	this.connect_helper();

});

// Target stated is disconnected.
Remote.method('disconnect', function(done) {
	var ws = this.ws;

	ws.onclose		= function() {
			if (this.trace)
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

	if (this.trace)
		console.log("remote: send: %s", JSON.stringify(command));

	ws.send(JSON.stringify(command));
});

Remote.method('ledger_closed', function(done) {
	this.request({ 'command' : 'ledger_closed' }, done);
});

// Get the current proposed ledger entry.  May be closed (and revised) at any time (even before returning).
// Only for use by unit tests.
Remote.method('ledger_current', function(done) {
	this.request({ 'command' : 'ledger_current' }, done);
});

// Submit a json transaction.
// done(value)
// <-> value: { 'status', status, 'result' : result, ... }
// done may be called up to 3 times.
Remote.method('submit', function(json, done) {
//	this.request(..., function() {
//		});
});

// done(value)
// --> value: { 'status', status, 'result' : result, ... }
// done may be called up to 3 times.
Remote.method('account_root', function(account_id, done) {
	this.request({
			'command' : 'ledger_current',
		}, function() {
		});
});

exports.Remote = Remote;
exports.remoteConfig = remoteConfig;

// vim:ts=4
