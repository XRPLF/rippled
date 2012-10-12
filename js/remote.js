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

// npm
var WebSocket = require('ws');

var amount    = require('./amount.js');
var Amount    = amount.Amount;

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

var remoteConfig = function (config, server, trace) {
  var serverConfig = config.servers[server];
  return new Remote(serverConfig.trusted, serverConfig.websocket_ip, serverConfig.websocket_port, config, trace);
};

var flags = {
  // OfferCreate flags:
  'tfPassive'		      : 0x00010000,

  // Payment flags:
  'tfCreateAccount'	      : 0x00010000,
  'tfPartialPayment'	      : 0x00020000,
  'tfLimitQuality'	      : 0x00040000,
  'tfNoRippleDirect'	      : 0x00080000,
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
};

// Target state is connectted.
// done(readyState):
// --> readyState: OPEN, CLOSED
Remote.prototype.connect = function (done, timeout) {
  var self = this;
  
  this.url = util.format("ws://%s:%s", this.websocket_ip, this.websocket_port);
  this.done = done;
  
  if (timeout) {
    if (this.trace) console.log("remote: expire: false");
    
    this.expire = false;
    
    setTimeout(function () {
      if (self.trace) console.log("remote: expire: timeout");
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

// Send a request. The request should lack the id.
// <-> request: what to send, consumed.
Remote.prototype.request = function (request, onDone, onFailure) {
  var self  = this;

  this.id += 1;   // Advance id.
  
  request.id = this.id;
  
  this.ws.response[request.id] = function (response) {
    if (self.trace) console.log("remote: response: %s", JSON.stringify(response));
    
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
  
  this.ws.send(JSON.stringify(request));
};

Remote.prototype.request_ledger_closed = function (onDone, onFailure) {
  assert(this.trusted);   // If not trusted, need to check proof.
  this.request({ 'command' : 'ledger_closed' }, onDone, onFailure);
};

// Get the current proposed ledger entry.  May be closed (and revised) at any time (even before returning).
// Only for use by unit tests.
Remote.prototype.request_ledger_current = function (onDone, onFailure) {
  this.request({ 'command' : 'ledger_current' }, onDone, onFailure);
};

// <-> request:
// --> ledger : optional
// --> ledger_index : optional
// --> type
Remote.prototype.request_ledger_entry = function (req, onDone, onFailure) {
  var self  = this;

  assert(this.trusted);   // If not trusted, need to check proof, maybe talk packet protocol.
  
  req.command = 'ledger_entry';
  
  if (req.ledger_closed)
  {
    // XXX Initial implementation no caching.
    this.request(req, onDone, onFailure);
  }
  // else if (req.ledger_index)
  else
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
	
	entry = this.ledgers.current.account_root[req.account];
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
	      self.ledgers.current.account_root[r.node.Account] = r.node;
	      break;
	    
	    default:
	      // This type not cached.
	      // nothing();
	      break;
	  }
	  onDone(r.node);
	}, onFailure);
    }
  }
};

// Submit a json transaction.
// done(value)
// XXX <-> value: { 'status', status, 'result' : result, ... }
Remote.prototype.submit = function (req, onDone, onFailure) {
  if (this.trace) console.log("remote: submit: %s", JSON.stringify(req));

  req.command = 'submit';
  
  if (req.secret && !this.trusted)
  {
    onFailure({ 'error' : 'untrustedSever', 'request' : req });
  }
  else
  {
    this.request(req, onDone, onFailure);
  }
};

//
// Higher level functions.
//

// Subscribe to a server to get the current and closed ledger.
// XXX Set up routine to update on notification.
Remote.prototype.server_subscribe = function (onDone, onFailure) {
  var self  = this;

  this.request(
    { 'command' : 'server_subscribe' },
    function (r) {
      self.ledger_current_index = r.ledger_current_index;
      self.ledger_closed        = r.ledger_closed;
      self.stand_alone          = r.stand_alone;
      onDone();
    },
    onFailure
  );
};

Remote.prototype.ledger_accept = function (onDone, onFailure) {
  if (this.stand_alone)
  {
    this.request(
      { 'command' : 'ledger_accept' },
      onDone,
      onFailure
    );
  }
  else {
    onFailure({ 'error' : 'notStandAlone' });
  }
};

// Refresh accounts[account].seq
// done(result);
Remote.prototype.account_seq = function (account, advance, onDone, onFailure) {
  var self = this;
  var account_root_entry = this.accounts[account];
  
  if (account_root_entry && account_root_entry.seq)
  {
    var seq = account_root_entry.seq;

    if (advance) account_root_entry.seq += 1;

    onDone(seq);
  }
  else
  {
    // Need to get the ledger entry.
    this.request_ledger_entry(
      {
	'ledger'	: this.ledger_closed,
	'type'		: 'account_root',
	'account_root'	: account
      },
      function (node) {
	// Extract the seqence number from the account root entry.
	var seq	= node.Sequence;

	if (!account_root_entry) self.accounts[account]  = {};

	self.accounts[account].seq = seq + !!advance;

	onDone(seq);
      },
      onFailure
    );
  }
};

// A submit that fills in the sequence number.
Remote.prototype.submit_seq = function (trans, onDirty, onDone, onFailure) {
  var self = this;

  // Get the next sequence number for the account.
  this.account_seq(trans.transaction.Account, true,
    function (seq) {
      trans.transaction.Sequence = seq;
      self.submit(trans, onDone, onFailure);
    },
    onFailure);
};

// Mark an account's root node as dirty.
Remote.prototype.dirty_account_root = function (account) {
  delete this.ledgers.current.account_root.account;
};

//
// Transactions
//

Remote.prototype.offer_create = function (secret, src, taker_pays, taker_gets, expiration, onDone) {
  var secret	  = this.config.accounts[src] ? this.config.accounts[src].secret : secret;
  var src_account = this.config.accounts[src] ? this.config.accounts[src].account : src;

  var transaction = {
      'TransactionType'	: 'OfferCreate',
      'Account'		: src_account,
      'Fee'		: fees.offer.to_json(),
      'TakerPays'	: taker_pays.to_json(),
      'TakerGets'	: taker_gets.to_json(),
    };

  if (expiration)
    transaction.Expiration  = expiration;

  this.submit_seq(
      {
	'transaction' : transaction,
	'secret' : secret,
      }, function () {
      }, onDone);
};

Remote.prototype.ripple_line_set = function (secret, src, limit, quaility_in, quality_out, onDone) {
  var secret	  = this.config.accounts[src] ? this.config.accounts[src].secret : secret;
  var src_account = this.config.accounts[src] ? this.config.accounts[src].account : src;

  var transaction = {
      'TransactionType'	: 'CreditSet',
      'Account'		: src_account,
      'Fee'		: fees['default'].to_json(),
    };

  if (limit)
      transaction.LimitAmount = limit.to_json();

  if (quaility_in)
      transaction.QualityIn   = quaility_in;

  if (quaility_out)
      transaction.QualityOut  = quaility_out;

  this.submit_seq(
      {
	'transaction' : transaction,
	'secret'      : secret,
      }, function () {
      }, onDone);
};

// --> create: is only valid if destination gets XNS.
Remote.prototype.send = function (secret, src, dst, deliver_amount, send_max, create, onDone) {
  var secret	  = this.config.accounts[src] ? this.config.accounts[src].secret : secret;
  var src_account = this.config.accounts[src] ? this.config.accounts[src].account : src;
  var dst_account = this.config.accounts[dst] ? this.config.accounts[dst].account : dst;

  var transaction = {
      'TransactionType' : 'Payment',
      'Account'		: src_account,
      'Fee'		: (create ? fees.account_create : fees['default']).to_json(),
      'Destination'	: dst_account,
      'Amount'		: deliver_amount.to_json(),
    };

  if (create)
      transaction.Flags	  = flags.tfCreateAccount;

  if (send_max)
      transaction.SendMax = send_max.to_json();

  this.submit_seq(
      {
	'transaction' : transaction,
	'secret' : secret,
      }, function () {
      }, onDone);
};

exports.Remote          = Remote;
exports.remoteConfig    = remoteConfig;
exports.fees            = fees;
exports.flags           = flags;

// vim:sw=2:sts=2:ts=8
