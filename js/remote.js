// Remote access to a server.
// - We never send binary data.
// - We use the W3C interface for node and browser compatibility:
//   http://www.w3.org/TR/websockets/#the-websocket-interface
//
// YYY Will later provide a network access which use multiple instances of this.
// YYY A better model might be to allow requesting a target state: keep connected or not.
//

// Node
var util      = require('util');
var events    = require('events');

// npm
var WebSocket = require('ws');

var amount    = require('./amount.js');
var Amount    = amount.Amount;

// Events emmitted:
// 'success'
// 'error'
//   'remoteError'
//   'remoteUnexpected'
//   'remoteDisconnected'
var Request = function (remote, command) {
  this.message	= {
    'command' : command,
    'id'      : undefined,
  };
  this.remote	= remote;

  this.on('request', this.request_default);
};

Request.prototype  = new events.EventEmitter;

// Return this.  node EventEmitter's on doesn't return this.
Request.prototype.on = function (e, c) {
  events.EventEmitter.prototype.on.call(this, e, c);

  return this;
};

// Send the request to a remote.
Request.prototype.request = function (remote) {
  this.emit('request', remote);
};

Request.prototype.request_default = function () {
  this.remote.request(this);
};

// Set the ledger for a request.
// - ledger_entry
Request.prototype.ledger = function (ledger) {
  this.message.ledger	    = ledger;

  return this;
};

// Set the ledger_index for a request.
// - ledger_entry
Request.prototype.ledger_index = function (ledger_index) {
  this.message.ledger_index  = ledger_index;

  return this;
};

Request.prototype.account_root = function (account) {
  this.message.account_root  = account;

  return this;
};

Request.prototype.index = function (hash) {
  this.message.index  = hash;

  return this;
};

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

Remote.prototype      = new events.EventEmitter;

var remoteConfig = function (config, server, trace) {
  var serverConfig = config.servers[server];

  return new Remote(serverConfig.trusted, serverConfig.websocket_ip, serverConfig.websocket_port, config, trace);
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

Remote.prototype.connect_helper = function () {
  var self = this;
  
  if (this.trace) console.log("remote: connect: %s", this.url);
  
  var ws = this.ws = new WebSocket(this.url);;
  
  ws.response = {};
  
  ws.onopen = function () {
    if (self.trace) console.log("remote: onopen: %s", ws.readyState);
    
    ws.onclose = undefined;
    ws.onerror = undefined;
    
    clearTimeout(self.connect_timer); delete self.connect_timer;
    clearTimeout(self.retry_timer); delete self.retry_timer;

    self.done(ws.readyState);
  };
  
  ws.onerror = function () {
    if (self.trace) console.log("remote: onerror: %s", ws.readyState);
    
    ws.onclose = undefined;
    
    if (self.expire) {
      if (self.trace) console.log("remote: was expired");

      self.done(ws.readyState);

    } else {
      // Delay and retry.

      clearTimeout(self.retry_timer);
      self.retry_timer  =  setTimeout(function () {
	  if (self.trace) console.log("remote: retry");

	  self.connect_helper();
	}, 50); // Retry rate 50ms.
    }
  };
  
  // Covers failure to open.
  ws.onclose = function () {
    if (self.trace) console.log("remote: onclose: %s", ws.readyState);

    ws.onerror = undefined;

    clearTimeout(self.retry_timer);
    delete self.retry_timer;

    self.done(ws.readyState);
  };
  
  // Node's ws module doesn't pass arguments to onmessage.
  ws.on('message', function (json, flags) {
    var message	    = JSON.parse(json);
    var	unexpected  = false;
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
	    if (self.trace) console.log("message: %s", json);

	    request.emit('success', message);
	  }
	  else if (message.error) {
	    if (self.trace) console.log("message: %s", json);

	    request.emit('error', {
		'error'		: 'remoteError',
		'error_message' : 'Remote reported an error.',
		'remote'        : message,
	      });
	  }
	}

	case 'ledgerClosed':
	  // XXX If not trusted, need to verify we consider ledger closed.
	  // XXX Also need to consider a slow server or out of order response.
	  // XXX Be more defensive fields could be missing or of wrong type.
	  // YYY Might want to do some cache management.

	  self.ledger_closed	    = message.ledger_closed;
	  self.ledger_current_index = message.ledger_closed_index + 1;
	  
	  self.emit('ledger_closed');
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
    else if (self.trusted) {
      // Remote is trusted, report an error.
      console.log("unexpected message from trusted remote: %s", json);

      (request || self).emit('error', {
	  'error' : 'remoteUnexpected',
	  'error_message' : 'Unexpected response from remote.'
	});
    }
    else {
      // Treat as a disconnect.
      if (self.trace) console.log("unexpected message from untrusted remote: %s", json);

      // XXX All pending request need this treatment and need to actionally disconnect.
      (request || self).emit('error', {
	  'error' : 'remoteDisconnected',
	  'error_message' : 'Remote disconnected.'
	});
    }
  });
};

// Target state is connectted.
// XXX Get rid of 'done' use event model.
// done(readyState):
// --> readyState: OPEN, CLOSED
Remote.prototype.connect = function (done, timeout) {
  var self = this;
  
  this.url  = util.format("ws://%s:%s", this.websocket_ip, this.websocket_port);
  this.done = done;
  
  if (timeout) {
    if (this.trace) console.log("remote: expire: false");
    
    this.expire		= false;

    this.connect_timer	= setTimeout(function () {
	if (self.trace) console.log("remote: expire: timeout");

	delete self.connect_timer;
	self.expire = true;
      }, timeout);

  } else {
    if (this.trace) console.log("remote: expire: false");
    this.expire = true;
  }
  
  this.connect_helper();
};

// Target stated is disconnected.
Remote.prototype.disconnect = function (done) {
  var self  = this;
  var ws    = this.ws;
  
  ws.onclose = function () {
    if (self.trace) console.log("remote: onclose: %s", ws.readyState);
    done(ws.readyState);
  };
  
  ws.close();
};

// Send a request.
// <-> request: what to send, consumed.
Remote.prototype.request = function (request) {
  var self  = this;

  this.ws.response[request.message.id = this.id] = request;
  
  this.id += 1;   // Advance id.
  
  if (this.trace) console.log("remote: request: %s", JSON.stringify(request.message));
  
  this.ws.send(JSON.stringify(request.message));
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

// <-> request:
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
      // XXX Initial implementation no caching.
    }
    // else if (req.ledger_index)
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

	this.request_default(remote);
      }
    }
  });

  return request;
};

// Submit a transaction.
Remote.prototype.submit = function (transaction) {
  debugger;
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
      var   cache_request = this.account_cache(transaction.transaction.Account);

      cache_request.on('success_account_cache', function () {
	    // Try again.
	    self.submit(transaction);
	});

      cache_request.on('error', function (message) {
	  // Forward errors.
	  transaction.emit('error', message);
	});

      cache_request.request();
    }
    else {
      var submit_request = new Request(this, 'submit');

      // Forward successes and errors.
      submit_request.on('success', function (message) { transaction.emit('success', message); });
      submit_request.on('error', function (message) { transaction.emit('error', message); });
    
      // XXX If transaction has a 'final' event listeners, register transaction to listen to final results.
      // XXX Final messages only happen if a transaction makes it into a ledger.
      // XXX   A transaction may be "lost" or even resubmitted in this case.
      // XXX For when ledger closes, can look up transaction meta data.
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
Remote.prototype.server_subscribe = function () {
  var self  = this;

  var request = new Request(this, 'server_subscribe');

  request.on('success', function (message) {
      self.ledger_current_index = message.ledger_current_index;
      self.ledger_closed        = message.ledger_closed;
      self.stand_alone          = message.stand_alone;

      self.emit('subscribed');

      self.emit('ledger_closed');
    });

  // XXX Could give error events, maybe even time out.

  return this;
};

// Ask the remote to accept the current ledger.
// - To be notified when the ledger is accepted, server_subscribe() then listen to 'ledger_closed' events.
Remote.prototype.ledger_accept = function () {
  if (this.stand_alone)
  {
    var request = new Request(this, 'ledger_accept');

    request.request();
  }
  else {
    self.emit('error', {
	'error' : 'notStandAlone'
      });
  }

  return this;
};

// Return the next account sequence if possible.
// <-- undefined or Sequence
Remote.prototype.account_seq = function (account, advance) {
  var account_info = this.accounts[account];
  var seq;

  if (account_info && account_info.seq)
  {
    var seq = account_info.seq;

    if (advance) account_root_entry.seq += 1;
  }

  return seq;
}

// Return a request to refresh accounts[account].seq.
Remote.prototype.account_cache = function (account) {
  var self    = this;
  var request = this.request_ledger_entry('account_root')

  // Only care about a closed ledger.
  // YYY Might be more advanced and work with a changing current ledger.
  request.ledger_closed	= this.ledger_closed;
  request.account_root	= account;

  request.on('success', function (message) {
      var seq = message.node.Sequence;
  
      self.accounts[account].seq  = seq;

      // If the caller also waits for 'success', they might run before this.
      request.emit('success_account_cache');
    });

    return request;
};

// Mark an account's root node as dirty.
Remote.prototype.dirty_account_root = function (account) {
  delete this.ledgers.current.account_root[account];
};

Remote.prototype.transaction = function () {
  return new Transaction(this);
};

//
// Transactions
//

// A class to implement transactions.
// - Collects parameters
// - Allow event listeners to be attached to determine the outcome.
var Transaction	= function (remote) {
  this.prototype    = events.EventEmitter;	// XXX Node specific.

  this.remote	    = remote;
  this.secret	    = undefined;
  this.transaction  = {};   // Transaction data.
};

Transaction.prototype  = new events.EventEmitter;

// Return this.  node EventEmitter's on doesn't return this.
Transaction.prototype.on = function (e, c) {
  events.EventEmitter.prototype.on.call(this, e, c);

  return this;
};

// Submit a transaction to the network.
Transaction.prototype.submit = function () {
  var transaction = this.transaction;

  // Fill in secret from config, if needed.
  if (undefined === transaction.secret && this.remote.config.accounts[this.Account]) {
    this.secret		      = this.remote.config.accounts[this.Account].secret;
  }

  if (undefined === transaction.Fee) {
    if ('Payment' === transaction.TransactionType
      && transaction.Flags & exports.flags.Payment.CreateAccount) {

      transaction.Fee      = fees.account_create.to_json();
    }
    else {
      transaction.Fee      = fees['default'].to_json();
    }
  }

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

      if (undefined == this.transaction.Flags)
	this.transaction.Flags	  = 0;

      for (flag in 'object' === typeof flags ? flags : [ flags ]) {
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
//  remote.transaction()    // Build a transaction object.
//     .offer_create(...)   // Set major parameters.
//     .flags()		    // Set optional parameters.
//     .on()		    // Register for events.
//     .submit();	    // Send to network.
//

// Allow config account defaults to be used.
Transaction.prototype.account_default = function (account) {
  return this.remote.config.accounts[account] ? this.remote.config.accounts[account].account : account;
};

Transaction.prototype.offer_create = function (src, taker_pays, taker_gets, expiration) {
  this.transaction.TransactionType  = 'OfferCreate';
  this.transaction.Account	    = this.account_default(src);
  this.transaction.Amount	    = deliver_amount.to_json();
  this.transaction.Destination	    = dst_account;
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
Transaction.prototype.payment = function (src, dst, deliver_amount) {

  this.transaction.TransactionType  = 'Payment';
  this.transaction.Account	    = this.account_default(src);
  this.transaction.Amount	    = deliver_amount.to_json();
  this.transaction.Destination	    = this.account_default(dst);

  return this;
}

Remote.prototype.ripple_line_set = function (src, limit, quaility_in, quality_out) {
  this.transaction.TransactionType  = 'CreditSet';
  this.transaction.Account	    = this.account_default(src);

  // Allow limit of 0 through.
  if (undefined !== limit)
    this.transaction.LimitAmount  = limit.to_json();

  if (quaility_in)
    this.transaction.QualityIn	  = quaility_in;

  if (quaility_out)
    this.transaction.QualityOut	  = quaility_out;

  // XXX Throw an error if nothing is set.

  return this;
};

exports.Remote          = Remote;
exports.remoteConfig    = remoteConfig;
exports.fees            = fees;
exports.flags           = flags;

// vim:sw=2:sts=2:ts=8
