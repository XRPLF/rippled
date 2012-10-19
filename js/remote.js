// Remote access to a server.
// - We never send binary data.
// - We use the W3C interface for node and browser compatibility:
//   http://www.w3.org/TR/websockets/#the-websocket-interface
//
// YYY Will later provide a network access which use multiple instances of this.
//

// Node
var util	  = require('util');
var EventEmitter  = require('events').EventEmitter;

// npm
var WebSocket = require('ws');

var Amount    = require('./amount.js').Amount;
var UInt160   = require('./amount.js').UInt160;

// Request events emmitted:
// 'success' : Request successful.
// 'error'   : Request failed.
//   'remoteError'
//   'remoteUnexpected'
//   'remoteDisconnected'
var Request = function (remote, command) {
  var self  = this;

  this.message	= {
    'command' : command,
    'id'      : undefined,
  };
  this.remote	= remote;

  this.on('request', function () {
      self.request_default();
    });
};

Request.prototype  = new EventEmitter;

// Return this.  node EventEmitter's on doesn't return this.
Request.prototype.on = function (e, c) {
  EventEmitter.prototype.on.call(this, e, c);

  return this;
};

Request.prototype.once = function (e, c) {
  EventEmitter.prototype.once.call(this, e, c);

  return this;
};

// Send the request to a remote.
Request.prototype.request = function (remote) {
  this.emit('request', remote);
};

Request.prototype.request_default = function () {
  this.remote.request(this);
};

Request.prototype.ledger_choose = function (current) {
  if (current)
  {
    this.message.ledger_index = this.remote.ledger_current_index;
  }
  else {
    this.message.ledger	      = this.remote.ledger_closed;
  }

  return this;
};

// Set the ledger for a request.
// - ledger_entry
// - transaction_entry
Request.prototype.ledger_closed = function (ledger) {
  this.message.ledger_closed  = ledger;

  return this;
};

// Set the ledger_index for a request.
// - ledger_entry
Request.prototype.ledger_index = function (ledger_index) {
  this.message.ledger_index  = ledger_index;

  return this;
};

Request.prototype.account_root = function (account) {
  this.message.account_root  = UInt160.from_json(account).to_json();

  return this;
};

Request.prototype.index = function (hash) {
  this.message.index  = hash;

  return this;
};

Request.prototype.secret = function (s) {
  if (s)
    this.message.secret  = s;

  return this;
};

Request.prototype.transaction = function (t) {
  this.message.transaction  = t;

  return this;
};

Request.prototype.ripple_state = function (account, issuer, currency) {
  this.message.ripple_state  = {
      'accounts' : [
	UInt160.from_json(account).to_json(),
	UInt160.from_json(issuer).to_json()
      ],
      'currency' : currency
    };

  return this;
};

//
// Remote - access to a remote Ripple server via websocket.
//
// Events:
// 'connectted'
// 'disconnected'
// 'state':
// - 'online' : connectted and subscribed
// - 'offline' : not subscribed or not connectted.
// 'ledger_closed': A good indicate of ready to serve.
// 'subscribed' : This indicates stand-alone is available.
//

// --> trusted: truthy, if remote is trusted
var Remote = function (trusted, websocket_ip, websocket_port, config, trace) {
  this.trusted              = trusted;
  this.websocket_ip         = websocket_ip;
  this.websocket_port       = websocket_port;
  this.id                   = 0;
  this.config               = config;
  this.trace                = trace;
  this.ledger_closed        = undefined;
  this.ledger_current_index = undefined;
  this.stand_alone          = undefined;
  this.online_target	    = false;
  this.online_state	    = 'closed';	  // 'open', 'closed', 'connecting', 'closing'
  this.state	  	    = 'offline';  // 'online', 'offline'
  this.retry_timer	    = undefined;
  this.retry		    = undefined;
  
  // Cache information for accounts.
  this.accounts = {
    // Consider sequence numbers stable if you know you're not generating bad transactions.
    // Otherwise, clear it to have it automatically refreshed from the network.
    
    // account : { seq : __ }

    };
  
  // Cache for various ledgers.
  // XXX Clear when ledger advances.
  this.ledgers = {
    'current' : {}
  };
};

Remote.prototype      = new EventEmitter;

var remoteConfig = function (config, server, trace) {
  var serverConfig = config.servers[server];

  return new Remote(serverConfig.trusted, serverConfig.websocket_ip, serverConfig.websocket_port, config, trace);
};

var isTemMalformed  = function (engine_result_code) {
  return (engine_result_code >= -299 && engine_result_code <  199);
};

var isTefFailure = function (engine_result_code) {
  return (engine_result_code >= -299 && engine_result_code <  199);
};

var flags = {
  'OfferCreate' : {
    'Passive'		      : 0x00010000,
  },

  'Payment' : {
    'CreateAccount'	      : 0x00010000,
    'PartialPayment'	      : 0x00020000,
    'LimitQuality'	      : 0x00040000,
    'NoRippleDirect'	      : 0x00080000,
  },
};

// XXX This needs to be determined from the network.
var fees = {
  'default'	    : Amount.from_json("100"),
  'account_create'  : Amount.from_json("1000"),
  'nickname_create' : Amount.from_json("1000"),
  'offer'	    : Amount.from_json("100"),
};

// Set the emited state: 'online' or 'offline'
Remote.prototype._set_state = function (state) {
  if (this.trace) console.log("remote: set_state: %s", state);

  if (this.state !== state) {
    this.state = state;

    this.emit('state', state);
    switch (state) {
      case 'online':
	this.online_state	= 'open';
	this.emit('connected');
	break;

      case 'offline':
	this.online_state	= 'closed';
	this.emit('disconnected');
	break;
    }
  }
};

// Set the target online state. Defaults to false.
Remote.prototype.connect = function (online) {
  var target  = undefined === online || online;

  if (this.online_target != target) {
    this.online_target  = target;

    // If we were in a stable state, go dynamic.
    switch (this.online_state) {
      case 'open':
	if (!target) this._connect_stop();
	break;

      case 'closed':
	if (target) this._connect_retry();
	break;
    }
  }

  return this;
};

// Stop from open state.
Remote.prototype._connect_stop = function () {
  delete this.ws.onerror;
  delete this.ws.onclose;

  this.ws.terminate();
  delete this.ws;

  this._set_state('offline');
};

// Implictly we are not connected.
Remote.prototype._connect_retry = function () {
  var self  = this;

  if (!self.online_target) {
    // Do not continue trying to connect.
    this._set_state('offline');
  }
  else if ('connecting' !== this.online_state) {
    // New to connecting state.
    this.online_state = 'connecting';
    this.retry	      = 0;
 
    this._connect_start();
  }
  else
  {
    // Delay and retry.
    this.retry	      += 1;
    this.retry_timer  =  setTimeout(function () {
	if (self.trace) console.log("remote: retry");

	if (self.online_target) {
	  self._connect_start();
	}
	else {
	  self._connect_retry();
	}
      }, this.retry < 40 ? 1000/20 : 1000); // 20 times per second for 2 seconds then once per second.
  }
};

Remote.prototype._connect_start = function () {
  // Note: as a browser client can't make encrypted connections to random ips
  // with self-signed certs as the user must have pre-approved the self-signed certs.

  var self = this;
  var url  = util.format("ws://%s:%s", this.websocket_ip, this.websocket_port);
  
  if (this.trace) console.log("remote: connect: %s", url);
  
  var ws = this.ws = new WebSocket(url);
  
  ws.response = {};
  
  ws.onopen = function () {
    if (self.trace) console.log("remote: onopen: %s: online_target=%s", ws.readyState, self.online_target);
    
    ws.onerror = function () {
      if (self.trace) console.log("remote: onerror: %s", ws.readyState);

      delete ws.onclose;

      self._connect_retry();
    };

    ws.onclose = function () {
      if (self.trace) console.log("remote: onclose: %s", ws.readyState);

      delete ws.onerror;

      self._connect_retry();
    };
 
    if (self.online_target) {
      self._set_state('online');

      // Note, we could get disconnected before tis go through.
      self._server_subscribe();	    // Automatically subscribe.
    }
    else {
      self._connect_stop();
    }
  };
  
  ws.onerror = function () {
    if (self.trace) console.log("remote: onerror: %s", ws.readyState);
 
    delete ws.onclose;
    
    self._connect_retry();
  };

  // Failure to open.
  ws.onclose = function () {
    if (self.trace) console.log("remote: onclose: %s", ws.readyState);

    delete ws.onerror;

    self._connect_retry();
  };
  
  // Node's ws module doesn't pass arguments to onmessage.
  ws.on('message', function (json, flags) {
      self._connect_message(ws, json, flags);
    });
};

// It is possible for messages to be dispatched after the connection is closed.
Remote.prototype._connect_message = function (ws, json, flags) {
  var message	  = JSON.parse(json);
  var unexpected  = false;
  var request;

  if ('object' !== typeof message) {
    unexpected  = true;
  }
  else {
    switch (message.type) {
      case 'response':
	{
	  request	  = ws.response[message.id];

	  if (!request) {
	    unexpected  = true;
	  }
	  else if ('success' === message.result) {
	    if (this.trace) console.log("message: %s", json);

	    request.emit('success', message);
	  }
	  else if (message.error) {
	    if (this.trace) console.log("message: %s", json);

	    request.emit('error', {
		'error'		: 'remoteError',
		'error_message' : 'Remote reported an error.',
		'remote'        : message,
	      });
	  }
	}
	break;

      case 'ledgerClosed':
	// XXX If not trusted, need to verify we consider ledger closed.
	// XXX Also need to consider a slow server or out of order response.
	// XXX Be more defensive fields could be missing or of wrong type.
	// YYY Might want to do some cache management.

	this.ledger_closed	  = message.ledger_closed;
	this.ledger_current_index = message.ledger_closed_index + 1;

	this.emit('ledger_closed', message.ledger_closed, message.ledger_closed_index);
	break;
      
      default:
	unexpected  = true;
	break;
    }
  }

  if (!unexpected) {
  }
  // Unexpected response from remote.
  // XXX This isn't so robust. Hard fails should probably only happen in a debugging scenairo.
  else if (this.trusted) {
    // Remote is trusted, report an error.
    console.log("unexpected message from trusted remote: %s", json);

    (request || this).emit('error', {
	'error' : 'remoteUnexpected',
	'error_message' : 'Unexpected response from remote.'
      });
  }
  else {
    // Treat as a disconnect.
    if (this.trace) console.log("unexpected message from untrusted remote: %s", json);

    // XXX All pending request need this treatment and need to actionally disconnect.
    (request || this).emit('error', {
	'error' : 'remoteDisconnected',
	'error_message' : 'Remote disconnected.'
      });
  }
};

// Send a request.
// <-> request: what to send, consumed.
Remote.prototype.request = function (request) {
  if (this.ws) {
    // Only bother if we are still connected.

    this.ws.response[request.message.id = this.id] = request;
    
    this.id += 1;   // Advance id.
    
    if (this.trace) console.log("remote: request: %s", JSON.stringify(request.message));
    
    this.ws.send(JSON.stringify(request.message));
  }
  else {
    if (this.trace) console.log("remote: request: DROPPING: %s", JSON.stringify(request.message));
  }
};

Remote.prototype.request_ledger_closed = function () {
  assert(this.trusted);   // If not trusted, need to check proof.

  return new Request(this, 'ledger_closed');
};

// Get the current proposed ledger entry.  May be closed (and revised) at any time (even before returning).
// Only for use by unit tests.
Remote.prototype.request_ledger_current = function () {
  return new Request(this, 'ledger_current');
};

// --> ledger : optional
// --> ledger_index : optional
Remote.prototype.request_ledger_entry = function (type) {
  assert(this.trusted);   // If not trusted, need to check proof, maybe talk packet protocol.
  
  var self    = this;
  var request = new Request(this, 'ledger_entry');

  if (type)
    this.type = type;

  // Transparent caching:
  request.on('request', function (remote) {	      // Intercept default request.
    if (this.ledger_closed) {
      // XXX Add caching.
    }
    // else if (req.ledger_index)
    // else if ('ripple_state' === this.type)	      // YYY Could be cached per ledger.
    else if ('account_root' === this.type) {
      var cache = self.ledgers.current.account_root;
      
      if (!cache)
      {
	cache = self.ledgers.current.account_root = {};
      }
      
      var node = self.ledgers.current.account_root[request.message.account_root];

      if (node) {
	// Emulate fetch of ledger entry.
	this.request.emit('success', {
	    // YYY Missing lots of fields.
	    'node' :  node,
	  });
      }
      else {
	// Was not cached.

	// XXX Only allow with trusted mode.  Must sync response with advance.
	switch (response.type) {
	  case 'account_root':
	    request.on('success', function (message) {
		// Cache node.
		self.ledgers.current.account_root[message.node.Account] = message.node;
	      });
	    break;
	    
	  default:
	    // This type not cached.
	}

	this.request_default();
      }
    }
  });

  return request;
};

Remote.prototype.request_transaction_entry = function (hash) {
  assert(this.trusted);   // If not trusted, need to check proof, maybe talk packet protocol.
  
  return (new Request(this, 'transaction_entry'))
    .transaction(hash);
};

// Submit a transaction.
Remote.prototype.submit = function (transaction) {
  var self  = this;

  if (this.trace) console.log("remote: submit: %s", JSON.stringify(transaction.transaction));

  if (transaction.secret && !this.trusted)
  {
    transaction.emit('error', {
	'result'	  : 'serverUntrusted',
	'result_message'  : "Attempt to give a secret to an untrusted server."
      });
  }
  else {
    if (!transaction.transaction.Sequence) {
      transaction.transaction.Sequence	= this.account_seq(transaction.transaction.Account, 'ADVANCE');
    }

    if (!transaction.transaction.Sequence) {
      // Look in the last closed ledger.
      this.account_seq_cache(transaction.transaction.Account, false)
	.on('success_account_seq_cache', function () {
	    // Try again.
	    self.submit(transaction);
	  })
	.on('error', function (message) {
	    // XXX Maybe be smarter about this. Don't want to trust an untrusted server for this seq number.

	    // Look in the current ledger.
	    self.account_seq_cache(transaction.transaction.Account, 'CURRENT')
	      .on('success_account_seq_cache', function () {
		  // Try again.
		  self.submit(transaction);
		})
	      .on('error', function (message) {
		  // Forward errors.
		  transaction.emit('error', message);
		})
	      .request();
	  })
	.request();
    }
    else {
      var submit_request = new Request(this, 'submit');

      submit_request.transaction(transaction.transaction);
      submit_request.secret(transaction.secret);

      // Forward successes and errors.
      submit_request.on('success', function (message) { transaction.emit('success', message); });
      submit_request.on('error', function (message) { transaction.emit('error', message); });
    
      submit_request.request();
    }
  }
};

//
// Higher level functions.
//

// Subscribe to a server to get 'ledger_closed' events.
// 'subscribed' : This command was successful.
// 'ledger_closed : ledger_closed and ledger_current_index are updated.
Remote.prototype._server_subscribe = function () {
  var self  = this;

  (new Request(this, 'server_subscribe'))
    .on('success', function (message) {
	self.stand_alone          = !!message.stand_alone;

	if (message.ledger_closed && message.ledger_current_index) {
	  self.ledger_closed        = message.ledger_closed;
	  self.ledger_current_index = message.ledger_current_index;

	  self.emit('ledger_closed', self.ledger_closed, self.ledger_current_index-1);
	}

	self.emit('subscribed');
      })
    .request();

  // XXX Could give error events, maybe even time out.

  return this;
};

// Ask the remote to accept the current ledger.
// - To be notified when the ledger is accepted, server_subscribe() then listen to 'ledger_closed' events.
Remote.prototype.ledger_accept = function () {
  if (this.stand_alone || undefined === this.stand_alone)
  {
    (new Request(this, 'ledger_accept'))
      .request();
  }
  else {
    this.emit('error', {
	'error' : 'notStandAlone'
      });
  }

  return this;
};

// Return a request to refresh the account balance.
Remote.prototype.request_account_balance = function (account, current) {
  return (this.request_ledger_entry('account_root'))
    .account_root(account)
    .ledger_choose(current)
    .on('success', function (message) {
      // If the caller also waits for 'success', they might run before this.
      request.emit('account_balance', message.node.Balance);
    });
};

// Return the next account sequence if possible.
// <-- undefined or Sequence
Remote.prototype.account_seq = function (account, advance) {
  var account_info = this.accounts[account];
  var seq;

  if (account_info && account_info.seq)
  {
    var seq = account_info.seq;

    if (advance) account_info.seq += 1;
  }

  return seq;
}

// Return a request to refresh accounts[account].seq.
Remote.prototype.account_seq_cache = function (account, current) {
  var self    = this;
  var request = this.request_ledger_entry('account_root');

  return request
    .account_root(account)
    .ledger_choose(current)
    .on('success', function (message) {
	var seq = message.node.Sequence;
    
	if (!self.accounts[account]) self.accounts[account] = {};

	self.accounts[account].seq  = seq;

	// If the caller also waits for 'success', they might run before this.
	request.emit('success_account_seq_cache');
      });
};

// Mark an account's root node as dirty.
Remote.prototype.dirty_account_root = function (account) {
  delete this.ledgers.current.account_root[account];
};

// Return a request to get a ripple balance.
//
// --> account: String
// --> issuer: String
// --> currency: String
// --> current: bool : true = current ledger
Remote.prototype.request_ripple_balance = function (account, issuer, currency, current) {
  var account_u	    = UInt160.from_json(account);
  var request	    = this.request_ledger_entry('ripple_state');	  // YYY Could be cached per ledger.

  return request
    .ripple_state(account, issuer, currency)
    .ledger_choose(current)
    .on('success', function (message) {
	var node	    = message.node;
	var lowLimit	    = Amount.from_json(node.LowLimit);
	var highLimit	    = Amount.from_json(node.HighLimit);
	var balance	    = Amount.from_json(node.Balance);
	var flip	    = account_u == highLimit.issuer;
	var issuerLimit	    = flip ? lowLimit : highLimit;
	var accountLimit    = flip ? highLimit : lowLimit;
	var issuerBalance   = (flip ? balance.negate() : balance).parse_issuer(issuer);
	var accountBalance  = issuerBalance.clone().parse_issuer(issuer);

	request.emit('ripple_state', {
	  'issuer_balance'  : issuerBalance,				  // Balance with dst as issuer.
	  'account_balance' : accountBalance,				  // Balance with account as issuer.
	  'issuer_limit'    : issuerLimit.clone().parse_issuer(account),  // Limit set by issuer with src as issuer.
	  'account_limit'   : accountLimit.clone().parse_issuer(issuer)	  // Limit set by account with dst as issuer.
	});
      });
}

Remote.prototype.transaction = function () {
  return new Transaction(this);
};

//
// Transactions
//
//  Construction:
//    remote.transaction()  // Build a transaction object.
//     .offer_create(...)   // Set major parameters.
//     .flags()		    // Set optional parameters.
//     .on()		    // Register for events.
//     .submit();	    // Send to network.
//
//  Events:
// 'success' : Transaction submitted without error.
// 'error' : Error submitting transaction.
// 'proposed: Advisory proposed status transaction.
// - A client should expect 0 to multiple results.
// - Might not get back. The remote might just forward the transaction.
// - A success could be reverted in final.
// - local error: other remotes might like it.
// - malformed error: local server thought it was malformed.
// - The client should only trust this when talking to a trusted server.
// 'final' : Final status of transaction.
// - Only expect a final from dishonest servers after a tesSUCCESS or ter*.
// 'lost' : Gave up looking for on ledger_closed.
// 'pending' : Transaction was not found on ledger_closed.
// 'state' : Follow the state of a transaction.
//    'client_submitted'     - Sent to remote
//     |- 'remoteError'	     - Remote rejected transaction.
//      \- 'client_proposed' - Remote provisionally accepted transaction.
//       |- 'client_missing' - Transaction has not appeared in ledger as expected.
//       | |\- 'client_lost' - No longer monitoring missing transaction.
//       |/
//       |- 'tesSUCCESS'     - Transaction in ledger as expected.
//       |- 'ter...'	     - Transaction failed.
//       \- 'tep...'	     - Transaction partially succeeded.
//
// Notes:
// - All transactions including those with local and malformed errors may be
//   forwarded anyway.
// - A malicous server can:
//   - give any proposed result.
//     - it may declare something correct as incorrect or something correct as incorrect.
//     - it may not communicate with the rest of the network.
//   - may or may not forward.
//

var SUBMIT_MISSING  = 4;    // Report missing.
var SUBMIT_LOST	    = 8;    // Give up tracking.

// A class to implement transactions.
// - Collects parameters
// - Allow event listeners to be attached to determine the outcome.
var Transaction	= function (remote) {
  var self  = this;

  this.prototype    = EventEmitter;	// XXX Node specific.

  this.remote	    = remote;
  this.secret	    = undefined;
  this.transaction  = {		      	// Transaction data.
    'Flags' : 0,		      	// XXX Would be nice if server did not require this.
  };
  this.hash	    = undefined;
  this.submit_index = undefined;      	// ledger_current_index was this when transaction was submited.
  this.state	    = undefined;	// Under construction.

  this.on('success', function (message) {
      if (message.engine_result) {
	self.hash	= message.transaction.hash;

	self.set_state('client_proposed');

	self.emit('proposed', {
	    'result'	      : message.engine_result,
	    'result_code'     : message.engine_result_code,
	    'result_message'  : message.engine_result_message,
	    'rejected'	      : self.isRejected(message.engine_result_code),	  // If server is honest, don't expect a final if rejected.
	  });
      }
    });

  this.on('error', function (message) {
	// Might want to give more detailed information.
	self.set_state('remoteError');
    });
};

Transaction.prototype  = new EventEmitter;

// Return this.  node EventEmitter's on doesn't return this.
Transaction.prototype.on = function (e, c) {
  EventEmitter.prototype.on.call(this, e, c);

  return this;
};

Transaction.prototype.consts = {
  'telLOCAL_ERROR'  : -399,
  'temMALFORMED'    : -299,
  'tefFAILURE'	    : -199,
  'terRETRY'	    : -99,
  'tesSUCCESS'	    : 0,
  'tepPARTIAL'	    : 100,
};

Transaction.prototype.isTelLocal = function (ter) {
  return ter >= this.consts.telLOCAL_ERROR && ter < this.consts.temMALFORMED;
};

Transaction.prototype.isTemMalformed = function (ter) {
  return ter >= this.consts.temMALFORMED && ter < this.consts.tefFAILURE;
};

Transaction.prototype.isTefFailure = function (ter) {
  return ter >= this.consts.tefFAILURE && ter < this.consts.terRETRY;
};

Transaction.prototype.isTerRetry = function (ter) {
  return ter >= this.consts.terRETRY && ter < this.consts.tesSUCCESS;
};

Transaction.prototype.isTepSuccess = function (ter) {
  return ter >= this.consts.tesSUCCESS;
};

Transaction.prototype.isTepPartial = function (ter) {
  return ter >= this.consts.tepPATH_PARTIAL;
};

Transaction.prototype.isRejected = function (ter) {
  return this.isTelLocal(ter) || this.isTemMalformed(ter) || this.isTefFailure(ter);
};

Transaction.prototype.set_state = function (state) {
  if (this.state !== state) {
    this.state  = state;
    this.emit('state', state);
  }
};

// Submit a transaction to the network.
// XXX Don't allow a submit without knowing ledger_closed_index.
// XXX Have a network canSubmit(), post events for following.
// XXX Also give broader status for tracking through network disconnects.
Transaction.prototype.submit = function () {
  var self	  = this;
  var transaction = this.transaction;

  if (undefined === transaction.Fee) {
    if ('Payment' === transaction.TransactionType
      && transaction.Flags & exports.flags.Payment.CreateAccount) {

      transaction.Fee    = fees.account_create.to_json();
    }
    else {
      transaction.Fee    = fees['default'].to_json();
    }
  }

  if (this.listeners('final').length || this.listeners('lost').length || this.listeners('pending').length) {
    // There are listeners for 'final', 'lost', or 'pending' arrange to emit them.

    this.submit_index = this.remote.ledger_current_index;

    var	on_ledger_closed = function (ledger_closed, ledger_closed_index) {
	var stop  = false;

// XXX make sure self.hash is available.
	self.remote.request_transaction_entry(self.hash)
	  .ledger_closed(ledger_closed)
	  .on('success', function (message) {
	      self.set_state(message.metadata.TransactionResult);
	      self.emit('final', message);
	    })
	  .on('error', function (message) {
	      if ('remoteError' === message.error
		&& 'transactionNotFound' === message.remote.error) {
		if (self.submit_index + SUBMIT_LOST < ledger_closed_index) {
		  self.set_state('client_lost');	// Gave up.
		  self.emit('lost');
		  stop  = true;
		}
		else if (self.submit_index + SUBMIT_MISSING < ledger_closed_index) {
		  self.set_state('client_missing');    // We don't know what happened to transaction, still might find.
		  self.emit('pending');
		}
		else {
		  self.emit('pending');
		}
	      }
	      // XXX Could log other unexpectedness.
	    })
	  .request();
	
	if (stop) {
	  self.removeListener('ledger_closed', on_ledger_closed);
	  self.emit('final', message);
	}
      };

    this.remote.on('ledger_closed', on_ledger_closed);
  }

  this.set_state('client_submitted');

  this.remote.submit(this);

  return this;
}

//
// Set options for Transactions
//

// If the secret is in the config object, it does not need to be provided.
Transaction.prototype.secret = function (secret) {
  this.secret = secret;
}

Transaction.prototype.send_max = function (send_max) {
  if (send_max)
      this.transaction.SendMax = send_max.to_json();

  return this;
}

// Add flags to a transaction.
// --> flags: undefined, _flag_, or [ _flags_ ]
Transaction.prototype.flags = function (flags) {
  if (flags) {
      var   transaction_flags = exports.flags[this.transaction.TransactionType];

      if (undefined == this.transaction.Flags)	// We plan to not define this field on new Transaction.
	this.transaction.Flags	  = 0;
      
      var flag_set  = 'object' === typeof flags ? flags : [ flags ];

      for (index in flag_set) {
	var flag  = flag_set[index];

	if (flag in transaction_flags)
	{
	  this.transaction.Flags      += transaction_flags[flag];
	}
	else {
	  // XXX Immediately report an error or mark it.
	}
      }

      if (this.transaction.Flags & exports.flags.Payment.CreateAccount)
	this.transaction.Fee	= fees.account_create.to_json();
  }

  return this;
}

//
// Transactions
//

// Allow config account defaults to be used.
Transaction.prototype.account_default = function (account) {
  return this.remote.config.accounts[account] ? this.remote.config.accounts[account].account : account;
};

Transaction.prototype.account_secret = function (account) {
  // Fill in secret from config, if needed.
  return this.remote.config.accounts[account] ? this.remote.config.accounts[account].secret : undefined;
};

Transaction.prototype.offer_create = function (src, taker_pays, taker_gets, expiration) {
  this.secret			    = this.account_secret(src);
  this.transaction.TransactionType  = 'OfferCreate';
  this.transaction.Account	    = UInt160.from_json(src).to_json();
  this.transaction.Fee		    = fees.offer.to_json();
  this.transaction.TakerPays	    = taker_pays.to_json();
  this.transaction.TakerGets	    = taker_gets.to_json();

  if (expiration)
    this.transaction.Expiration  = expiration;

  return this;
};

// Construct a 'payment' transaction.
//
// When a transaction is submitted:
// - If the connection is reliable and the server is not merely forwarding and is not malicious, 
// --> src : UInt160 or String
// --> dst : UInt160 or String
// --> deliver_amount : Amount or String.
Transaction.prototype.payment = function (src, dst, deliver_amount) {
  this.secret			    = this.account_secret(src);
  this.transaction.TransactionType  = 'Payment';
  this.transaction.Account	    = UInt160.from_json(src).to_json();
  this.transaction.Amount	    = Amount.from_json(deliver_amount).to_json();
  this.transaction.Destination	    = UInt160.from_json(dst).to_json();

  return this;
}

Transaction.prototype.ripple_line_set = function (src, limit, quality_in, quality_out) {
  this.secret			    = this.account_secret(src);
  this.transaction.TransactionType  = 'CreditSet';
  this.transaction.Account	    = UInt160.from_json(src).to_json();

  // Allow limit of 0 through.
  if (undefined !== limit)
    this.transaction.LimitAmount  = limit.to_json();

  if (quality_in)
    this.transaction.QualityIn	  = quality_in;

  if (quality_out)
    this.transaction.QualityOut	  = quality_out;

  // XXX Throw an error if nothing is set.

  return this;
};

exports.Remote          = Remote;
exports.remoteConfig    = remoteConfig;
exports.fees            = fees;
exports.flags           = flags;

// vim:sw=2:sts=2:ts=8
