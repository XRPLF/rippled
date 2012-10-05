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
var Remote = function (trusted, websocket_ip, websocket_port, trace) {
  this.trusted              = trusted;
  this.websocket_ip         = websocket_ip;
  this.websocket_port       = websocket_port;
  this.id                   = 0;
  this.trace                = trace;
  this.ledger_closed        = undefined;
  this.ledger_current_index = undefined;
  this.stand_alone          = undefined;
  
  // Cache information for accounts.
  this.account = {
    // Consider sequence numbers stable if you know you're not generating bad transactions.
    // Otherwise, clear it to have it automatically refreshed from the network.
    
    // acount : { seq : __ }

    };
  
  // Cache for various ledgers.
  // XXX Clear when ledger advances.
  this.ledgers = {
    'current' : {}
  };
};

var remoteConfig = function (config, server, trace) {
  var serverConfig = config.servers[server];
  return new Remote(serverConfig.trusted, serverConfig.websocket_ip, serverConfig.websocket_port, trace);
};

// XXX This needs to be determined from the network.
var fees = {
  'default' : 100,
  'account_create' : 1000,
  'nickname_create' : 1000,
  'offer' : 100,
};

Remote.method('connect_helper', function () {
  var self = this;
  
  if (this.trace) console.log("remote: connect: %s", this.url);
  
  var ws = this.ws = new WebSocket(this.url);;
  
  ws.response = {};
  
  ws.onopen = function () {
    if (this.trace) console.log("remote: onopen: %s", ws.readyState);
    
    ws.onclose = undefined;
    ws.onerror = undefined;
    
    self.done(ws.readyState);
  };
  
  ws.onerror = function () {
    if (this.trace) console.log("remote: onerror: %s", ws.readyState);
    
    ws.onclose = undefined;
    
    if (self.expire) {
      if (this.trace) console.log("remote: was expired");
      self.done(ws.readyState);
    } else {
      // Delay and retry.
      setTimeout(function () {
	if (this.trace) console.log("remote: retry");
	self.connect_helper();
      }, 50); // Retry rate 50ms.
    }
  };
  
  // Covers failure to open.
  ws.onclose = function () {
    if (this.trace) console.log("remote: onclose: %s", ws.readyState);
    ws.onerror = undefined;
    self.done(ws.readyState);
  };
  
  // Node's ws module doesn't pass arguments to onmessage.
  ws.on('message', function (json, flags) {
    var message = JSON.parse(json);
    // console.log("message: %s", json);
    
    if (message.type !== 'response') {
      console.log("unexpected message: %s", json);
    } else {
      var done = ws.response[message.id];
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
Remote.method('connect', function (done, timeout) {
  var self = this;
  
  this.url = util.format("ws://%s:%s", this.websocket_ip, this.websocket_port);
  this.done = done;
  
  if (timeout) {
    if (this.trace) console.log("remote: expire: false");
    
    this.expire = false;
    
    setTimeout(function () {
      if (this.trace) console.log("remote: expire: timeout");
      self.expire = true;
    }, timeout);
  } else {
    if (this.trace) console.log("remote: expire: false");
    this.expire = true;
  }
  
  this.connect_helper();
});

// Target stated is disconnected.
Remote.method('disconnect', function (done) {
  var ws = this.ws;
  
  ws.onclose = function () {
    if (this.trace) console.log("remote: onclose: %s", ws.readyState);
    done(ws.readyState);
  };
  
  ws.close();
});

// Send a request. The request should lack the id.
// <-> request: what to send, consumed.
Remote.method('request', function (request, onDone, onFailure) {
  this.id += 1;   // Advance id.
  
  var ws = this.ws;
  
  request.id = this.id;
  
  ws.response[request.id] = function (response) {
    if (this.trace) console.log("remote: response: %s", JSON.stringify(response));
    
    if (onFailure && response.error)
    {
      onFailure(response);
    }
    else
    {
      onDone(response);
    }
  };
  
  if (this.trace) console.log("remote: request: %s", JSON.stringify(request));
  
  ws.send(JSON.stringify(request));
});

Remote.method('request_ledger_closed', function (onDone, onFailure) {
  assert(this.trusted);   // If not trusted, need to check proof.
  this.request({ 'command' : 'ledger_closed' }, onDone, onFailure);
});

// Get the current proposed ledger entry.  May be closed (and revised) at any time (even before returning).
// Only for use by unit tests.
Remote.method('request_ledger_current', function (onDone, onFailure) {
  this.request({ 'command' : 'ledger_current' }, onDone, onFailure);
});

// <-> request:
// --> ledger : optional
// --> ledger_index : optional
// --> type
Remote.method('request_ledger_entry', function (req, onDone, onFailure) {
  assert(this.trusted);   // If not trusted, need to check proof, maybe talk packet protocol.
  
  req.command = 'ledger_entry';
  
  if (req.ledger_closed)
  {
    // XXX Initial implementation no caching.
    this.request(req, onDone, onFailure);
  }
  else if (req.ledger_index)
  {
    // Current
    // XXX Only allow with standalone mode.  Must sync response with advance.
    var entry;
    
    switch (req.type) {
      case 'account_root':
	var cache = this.ledgers.current.account_root;
	
	if (!cache)
	{
	  cache = this.ledgers.current.account_root = {};
	}
	
	var entry = this.ledgers.current.account_root[req.account];
	break;
	
      default:
	// This type not cached.
    }
    
    if (entry)
    {
      onDone(entry);
    }
    else
    {
      // Not cached.
     
      // Submit request
      this.request(req, function (r) {
	// Got result.
	switch (req.type) {
	  case 'account_root':
	    this.ledgers.current.account_root.account = r;
	    break;
	  
	  default:
	    // This type not cached.
	}
	onDone(r);
      }, onFailure);
    }
  }
});

// Submit a json transaction.
// done(value)
// XXX <-> value: { 'status', status, 'result' : result, ... }
Remote.method('submit', function (request, onDone, onFailure) {
  var req = {};
  
  req.command = 'submit';
  req.request = request;
  
  if (req.secret && !this.trusted)
  {
    onFailure({ 'error' : 'untrustedSever', 'request' : req });
  }
  else
  {
    this.request(req, onDone, onFailure);
  }
});

//
// Higher level functions.
//

// Subscribe to a server to get the current and closed ledger.
// XXX Set up routine to update on notification.
Remote.method('server_subscribe', function (onDone, onFailure) {
  this.request(
    { 'command' : 'server_subscribe' },
      function (r) {
	this.ledger_current_index = r.ledger_current_index;
	this.ledger_closed        = r.ledger_closed;
	this.stand_alone          = r.stand_alone;
	onDone();
    },
    onFailure
  );
});

// Refresh accounts[account].seq
// done(result);
Remote.method('account_seq', function (account, advance, onDone, onFailure) {
  var account_root_entry = this.accounts[account];
  
  if (account_root_entry && account_root_entry.seq)
  {
    var seq = account_root_entry.seq;

    if (advance) account_root_entry.seq += 1;

    onDone(advance);
  }
  else
  {
    // Need to get the ledger entry.
    this.request_ledger_entry(
      {
	'ledger' : this.ledger_closed,
	'account_root' : account
      },
      function (r) {
	// Extract the seqence number from the account root entry.
	var seq	= r.seq;

	this.accounts[account].seq = seq + 1;

	onDone(seq);
      },
      onFailure
    );
  }
});

// A submit that fills in the sequence number.
Remote.method('submit_seq', function (transaction, onDirty, onDone, onFailure) {
  // Get the next sequence number for the account.
  this.account_seq(transaction.Signer, true,
    function (seq) {
      request.seq = seq;
      this.submit(onDone, onFailure);
    },
    onFailure);
});

// Mark an account's root node as dirty.
Remote.method('dirty_account_root', function (account) {
  delete this.ledgers.current.account_root.account;
});

exports.Remote          = Remote;
exports.remoteConfig    = remoteConfig;
exports.fees            = fees;

// vim:sw=2:sts=2:ts=8
