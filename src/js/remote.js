// Remote access to a server.
// - We never send binary data.
// - We use the W3C interface for node and browser compatibility:
//   http://www.w3.org/TR/websockets/#the-websocket-interface
//
// This class is intended for both browser and node.js use.
//
// This class is designed to work via peer protocol via either the public or
// private websocket interfaces.  The JavaScript class for the peer protocol
// has not yet been implemented. However, this class has been designed for it
// to be a very simple drop option.
//
// YYY Will later provide js/network.js which will transparently use multiple
// instances of this class for network access.
//

// npm
var EventEmitter  = require('events').EventEmitter;
var Server        = require('./server').Server;
var Amount        = require('./amount').Amount;
var Currency      = require('./currency').Currency;
var UInt160       = require('./uint160').UInt160;
var Transaction   = require('./transaction').Transaction;
var Account       = require('./account').Account;
var Meta          = require('./meta').Meta;
var OrderBook     = require('./orderbook').OrderBook;

var utils         = require('./utils');
var config        = require('./config');
var sjcl          = require('../../build/sjcl');

// Request events emitted:
// 'success' : Request successful.
// 'error'   : Request failed.
//   'remoteError'
//   'remoteUnexpected'
//   'remoteDisconnected'
var Request = function (remote, command) {
  var self  = this;

  this.message    = {
    'command' : command,
    'id'      : undefined,
  };
  this.remote     = remote;
  this.requested  = false;
};

Request.prototype  = new EventEmitter;

// Send the request to a remote.
Request.prototype.request = function (remote) {
  if (!this.requested) {
    this.requested  = true;
    this.remote.request(this);
    this.emit('request', remote);
  }
};

Request.prototype.build_path = function (build) {
  if (build)
    this.message.build_path = true;

  return this;
};

Request.prototype.ledger_choose = function (current) {
  if (current)
  {
    this.message.ledger_index = this.remote._ledger_current_index;
  }
  else {
    this.message.ledger_hash  = this.remote._ledger_hash;
  }

  return this;
};

// Set the ledger for a request.
// - ledger_entry
// - transaction_entry
Request.prototype.ledger_hash = function (h) {
  this.message.ledger_hash  = h;

  return this;
};

// Set the ledger_index for a request.
// - ledger_entry
Request.prototype.ledger_index = function (ledger_index) {
  this.message.ledger_index  = ledger_index;

  return this;
};

Request.prototype.ledger_select = function (ledger_spec) {
  if (ledger_spec === 'current') {
    this.message.ledger_index  = ledger_spec;

  } else if (ledger_spec === 'closed') {
    this.message.ledger_index  = ledger_spec;

  } else if (ledger_spec === 'verified') {
    this.message.ledger_index  = ledger_spec;

  } else if (String(ledger_spec).length > 12) { // XXX Better test needed
    this.message.ledger_hash  = ledger_spec;

  } else {
    this.message.ledger_index  = ledger_spec;
  }

  return this;
};

Request.prototype.account_root = function (account) {
  this.message.account_root  = UInt160.json_rewrite(account);

  return this;
};

Request.prototype.index = function (hash) {
  this.message.index  = hash;

  return this;
};

// Provide the information id an offer.
// --> account
// --> seq : sequence number of transaction creating offer (integer)
Request.prototype.offer_id = function (account, seq) {
  this.message.offer = {
    'account' : UInt160.json_rewrite(account),
    'seq' : seq
  };

  return this;
};

// --> index : ledger entry index.
Request.prototype.offer_index = function (index) {
  this.message.offer  = index;

  return this;
};

Request.prototype.secret = function (s) {
  if (s)
    this.message.secret  = s;

  return this;
};

Request.prototype.tx_hash = function (h) {
  this.message.tx_hash  = h;

  return this;
};

Request.prototype.tx_json = function (j) {
  this.message.tx_json  = j;

  return this;
};

Request.prototype.tx_blob = function (j) {
  this.message.tx_blob  = j;

  return this;
};

Request.prototype.ripple_state = function (account, issuer, currency) {
  this.message.ripple_state  = {
      'accounts' : [
        UInt160.json_rewrite(account),
        UInt160.json_rewrite(issuer)
      ],
      'currency' : currency
    };

  return this;
};

Request.prototype.accounts = function (accounts, realtime) {
  if ("object" !== typeof accounts) {
    accounts = [accounts];
  }

  // Process accounts parameters
  var procAccounts = [];
  for (var i = 0, l = accounts.length; i < l; i++) {
    procAccounts.push(UInt160.json_rewrite(accounts[i]));
  }
  if (realtime) {
    this.message.rt_accounts = procAccounts;
  } else {
    this.message.accounts = procAccounts;
  }

  return this;
};

Request.prototype.rt_accounts = function (accounts) {
  return this.accounts(accounts, true);
};

Request.prototype.books = function (books, snapshot) {
  var procBooks = [];

  for (var i = 0, l = books.length; i < l; i++) {
    var book = books[i];
    var json = {};

    function process(side) {
      if (!book[side]) throw new Error("Missing "+side);

      var obj = {};
      obj["currency"] = Currency.json_rewrite(book[side]["currency"]);
      if (obj["currency"] !== "XRP") {
        obj.issuer = UInt160.json_rewrite(book[side]["issuer"]);
      }

      json[side] = obj;
    }

    process("taker_gets");
    process("taker_pays");

    if (snapshot || book["snapshot"]) json["snapshot"] = true;
    if (book["both"]) json["both"] = true;

    procBooks.push(json);
  }
  this.message.books = procBooks;

  return this;
};

//
// Remote - access to a remote Ripple server via websocket.
//
// Events:
// 'connect'
// 'connected' (DEPRECATED)
// 'disconnect'
// 'disconnected' (DEPRECATED)
// 'state':
// - 'online'        : Connected and subscribed.
// - 'offline'       : Not subscribed or not connected.
// 'subscribed'      : This indicates stand-alone is available.
//
// Server events:
// 'ledger_closed'   : A good indicate of ready to serve.
// 'transaction'     : Transactions we receive based on current subscriptions.
// 'transaction_all' : Listening triggers a subscribe to all transactions
//                     globally in the network.

// --> trusted: truthy, if remote is trusted
var Remote = function (opts, trace) {
  var self  = this;

  this.trusted                = opts.trusted;
  this.websocket_ip           = opts.websocket_ip;
  this.websocket_port         = opts.websocket_port;
  this.websocket_ssl          = opts.websocket_ssl;
  this.local_sequence         = opts.local_sequence; // Locally track sequence numbers
  this.local_fee              = opts.local_fee;      // Locally set fees
  this.local_signing          = opts.local_signing;
  this.id                     = 0;
  this.trace                  = opts.trace || trace;
  this._server_fatal          = false;              // True, if we know server exited.
  this._ledger_current_index  = undefined;
  this._ledger_hash           = undefined;
  this._ledger_time           = undefined;
  this._stand_alone           = undefined;
  this._testnet               = undefined;
  this._transaction_subs      = 0;
  this.online_target          = false;
  this._online_state          = 'closed';         // 'open', 'closed', 'connecting', 'closing'
  this.state                  = 'offline';        // 'online', 'offline'
  this.retry_timer            = undefined;
  this.retry                  = undefined;

  this._load_base             = 256;
  this._load_factor           = 1.0;
  this._fee_ref               = undefined;
  this._fee_base              = undefined;
  this._reserve_base          = undefined;
  this._reserve_inc           = undefined;
  this._connection_count      = 0;

  this._last_tx               = null;

  // Local signing implies local fees and sequences
  if (this.local_signing) {
    this.local_sequence = true;
    this.local_fee = true;
  }

  this._servers = [];

  // Cache information for accounts.
  // DEPRECATED, will be removed
  this.accounts = {
    // Consider sequence numbers stable if you know you're not generating bad transactions.
    // Otherwise, clear it to have it automatically refreshed from the network.

    // account : { seq : __ }

    };

  // Hash map of Account objects by AccountId.
  this._accounts = {};

  // Hash map of OrderBook objects
  this._books = {};

  // List of secrets that we know about.
  this.secrets = {
    // Secrets can be set by calling set_secret(account, secret).

    // account : secret
  };

  // Cache for various ledgers.
  // XXX Clear when ledger advances.
  this.ledgers = {
    'current' : {
      'account_root' : {}
    }
  };

  // XXX Add support for multiple servers
  var url  = (this.websocket_ssl ? "wss://" : "ws://") +
        this.websocket_ip + ":" + this.websocket_port;

  this.add_server(new Server(this, {url: url}));

  this.on('newListener', function (type, listener) {
      if ('transaction_all' === type)
      {
        if (!self._transaction_subs && 'open' === self._online_state)
        {
          self.request_subscribe([ 'transactions' ])
            .request();
        }
        self._transaction_subs  += 1;
      }
    });

  this.on('removeListener', function (type, listener) {
      if ('transaction_all' === type)
      {
        self._transaction_subs  -= 1;

        if (!self._transaction_subs && 'open' === self._online_state)
        {
          self.request_unsubscribe([ 'transactions' ])
            .request();
        }
      }
    });
};

Remote.prototype      = new EventEmitter;

Remote.from_config = function (obj, trace) {
  var serverConfig = 'string' === typeof obj ? config.servers[obj] : obj;

  var remote = new Remote(serverConfig, trace);

  for (var account in config.accounts) {
    var accountInfo = config.accounts[account];
    if ("object" === typeof accountInfo) {
      if (accountInfo.secret) {
        // Index by nickname ...
        remote.set_secret(account, accountInfo.secret);
        // ... and by account ID
        remote.set_secret(accountInfo.account, accountInfo.secret);
      }
    }
  }

  return remote;
};

var isTemMalformed  = function (engine_result_code) {
  return (engine_result_code >= -299 && engine_result_code <  199);
};

var isTefFailure = function (engine_result_code) {
  return (engine_result_code >= -299 && engine_result_code <  199);
};

Remote.prototype.add_server = function (server) {
  var self = this;

  server.on('message', function (data) {
    self._handle_message(data);
  });

  server.on('connect', function () {
    self._connection_count++;
    self._set_state('online');
  });

  server.on('disconnect', function () {
    self._connection_count--;
    if (!self._connection_count) self._set_state('offline');
  });

  this._servers.push(server);

  return this;
};

// Inform remote that the remote server is not comming back.
Remote.prototype.server_fatal = function () {
  this._server_fatal = true;
};

// Set the emitted state: 'online' or 'offline'
Remote.prototype._set_state = function (state) {
  if (this.trace) console.log("remote: set_state: %s", state);

  if (this.state !== state) {
    this.state = state;

    this.emit('state', state);

    switch (state) {
      case 'online':
        this._online_state       = 'open';
        this.emit('connect');
        this.emit('connected');
        break;

      case 'offline':
        this._online_state       = 'closed';
        this.emit('disconnect');
        this.emit('disconnected');
        break;
    }
  }
};

Remote.prototype.set_trace = function (trace) {
  this.trace  = undefined === trace || trace;

  return this;
};

/**
 * Connect to the Ripple network.
 */
Remote.prototype.connect = function (online) {
  // Downwards compatibility
  if ("undefined" !== typeof online && !online) {
    return this.disconnect();
  }

  if (!this._servers.length) {
    throw new Error("No servers available.");
  } else {
    // XXX Add support for multiple servers
    this._servers[0].connect();
  }

  return this;
};

/**
 * Disconnect from the Ripple network.
 */
Remote.prototype.disconnect = function (online) {
  for (var i = 0, l = this._servers.length; i < l; i++) {
    this._servers[i].disconnect();
  }

  this._set_state('offline');

  return this;
};

Remote.prototype.ledger_hash = function () {
  return this._ledger_hash;
};

// It is possible for messages to be dispatched after the connection is closed.
Remote.prototype._handle_message = function (json) {
  var self        = this;
  var message     = JSON.parse(json);
  var unexpected  = false;
  var request;

  if ('object' !== typeof message) {
    unexpected  = true;
  }
  else {
    switch (message.type) {
      case 'response':
        // Handled by the server that sent the request
        break;

      case 'ledgerClosed':
        // XXX If not trusted, need to verify we consider ledger closed.
        // XXX Also need to consider a slow server or out of order response.
        // XXX Be more defensive fields could be missing or of wrong type.
        // YYY Might want to do some cache management.

        this._ledger_time           = message.ledger_time;
        this._ledger_hash           = message.ledger_hash;
        this._ledger_current_index  = message.ledger_index + 1;

        this.emit('ledger_closed', message);
        break;

      case 'transaction':
        // To get these events, just subscribe to them. A subscribes and
        // unsubscribes will be added as needed.
        // XXX If not trusted, need proof.

        // De-duplicate transactions that are immediately following each other
        // XXX Should have a cache of n txs so we can dedup out of order txs
        if (this._last_tx === message.transaction.hash) break;
        this._last_tx = message.transaction.hash;

        if (this.trace) utils.logObject("remote: tx: %s", message);

        // Process metadata
        message.mmeta = new Meta(message.meta);

        // Pass the event on to any related Account objects
        var affected = message.mmeta.getAffectedAccounts();
        for (var i = 0, l = affected.length; i < l; i++) {
          var account = self._accounts[affected[i]];

          if (account) account.notifyTx(message);
        }

        // Pass the event on to any related OrderBooks
        affected = message.mmeta.getAffectedBooks();
        for (i = 0, l = affected.length; i < l; i++) {
          var book = self._books[affected[i]];

          if (book) book.notifyTx(message);
        }

        this.emit('transaction', message);
        this.emit('transaction_all', message);
        break;

      // XXX Should be tracked by the Server object
      case 'serverStatus':
        if ('load_base' in message &&
            'load_factor' in message &&
            (message.load_base !== self._load_base || message.load_factor != self._load_factor))
        {
          self._load_base     = message.load_base;
          self._load_factor   = message.load_factor;

          self.emit('load', { 'load_base' : self._load_base, 'load_factor' : self.load_factor });
        }
        break;

      // All other messages
      default:
        if (this.trace) utils.logObject("remote: "+message.type+": %s", message);
        this.emit('net_'+message.type, message);
        break;
    }
  }

  // Unexpected response from remote.
  if (unexpected) {
    console.log("unexpected message from trusted remote: %s", json);

    (request || this).emit('error', {
        'error' : 'remoteUnexpected',
        'error_message' : 'Unexpected response from remote.'
      });
  }
};

// Send a request.
// <-> request: what to send, consumed.
Remote.prototype.request = function (request) {
  if (!this._servers.length) {
    throw new Error("No servers available.");
  } else {
    // XXX Add support for multiple servers
    this._servers[0].request(request);
  }
};

Remote.prototype.request_server_info = function () {
  return new Request(this, 'server_info');
};

// XXX This is a bad command. Some varients don't scale.
// XXX Require the server to be trusted.
Remote.prototype.request_ledger = function (ledger, opts) {
  //utils.assert(this.trusted);

  var request = new Request(this, 'ledger');

  if (ledger)
  {
    // DEPRECATED: use .ledger_hash() or .ledger_index()
    console.log("request_ledger: ledger parameter is deprecated");
    request.message.ledger  = ledger;
  }

  if ('object' == typeof opts) {
    if (opts.full)
      request.message.full          = true;
  
    if (opts.expand)
      request.message.expand        = true;
  
    if (opts.transactions)
      request.message.transactions  = true;

    if (opts.accounts)
      request.message.accounts      = true;
  }
  // DEPRECATED:
  else if (opts)
  {
    console.log("request_ledger: full parameter is deprecated");
    request.message.full    = true;
  }

  return request;
};

// Only for unit testing.
Remote.prototype.request_ledger_hash = function () {
  //utils.assert(this.trusted);   // If not trusted, need to check proof.

  return new Request(this, 'ledger_closed');
};

// .ledger()
// .ledger_index()
Remote.prototype.request_ledger_header = function () {
  return new Request(this, 'ledger_header');
};

// Get the current proposed ledger entry.  May be closed (and revised) at any time (even before returning).
// Only for unit testing.
Remote.prototype.request_ledger_current = function () {
  return new Request(this, 'ledger_current');
};

// --> type : the type of ledger entry.
// .ledger()
// .ledger_index()
// .offer_id()
Remote.prototype.request_ledger_entry = function (type) {
  //utils.assert(this.trusted);   // If not trusted, need to check proof, maybe talk packet protocol.

  var self    = this;
  var request = new Request(this, 'ledger_entry');

  // Transparent caching. When .request() is invoked, look in the Remote object for the result.
  // If not found, listen, cache result, and emit it.
  //
  // Transparent caching:
  if ('account_root' === type) {
    request.request_default = request.request;

    request.request         = function () {                        // Intercept default request.
      var bDefault  = true;
      // .self = Remote
      // this = Request

      // console.log('request_ledger_entry: caught');

      if (self._ledger_hash) {
        // A specific ledger is requested.

        // XXX Add caching.
      }
      // else if (req.ledger_index)
      // else if ('ripple_state' === request.type)         // YYY Could be cached per ledger.
      else if ('account_root' === type) {
        var cache = self.ledgers.current.account_root;

        if (!cache)
        {
          cache = self.ledgers.current.account_root = {};
        }

        var node = self.ledgers.current.account_root[request.message.account_root];

        if (node) {
          // Emulate fetch of ledger entry.
          // console.log('request_ledger_entry: emulating');
          request.emit('success', {
              // YYY Missing lots of fields.
              'node' :  node,
            });

          bDefault  = false;
        }
        else {
          // Was not cached.

          // XXX Only allow with trusted mode.  Must sync response with advance.
          switch (type) {
            case 'account_root':
              request.on('success', function (message) {
                  // Cache node.
                  // console.log('request_ledger_entry: caching');
                  self.ledgers.current.account_root[message.node.Account] = message.node;
                });
              break;

            default:
              // This type not cached.
              // console.log('request_ledger_entry: non-cached type');
          }
        }
      }

      if (bDefault) {
        // console.log('request_ledger_entry: invoking');
        request.request_default();
      }
    }
  };

  return request;
};

// .accounts(accounts, realtime)
Remote.prototype.request_subscribe = function (streams) {
  var request = new Request(this, 'subscribe');

  if (streams) {
    if ("object" !== typeof streams) {
      streams = [streams];
    }
    request.message.streams = streams;
  }

  return request;
};

// .accounts(accounts, realtime)
Remote.prototype.request_unsubscribe = function (streams) {
  var request = new Request(this, 'unsubscribe');

  if (streams) {
    if ("object" !== typeof streams) {
      streams = [streams];
    }
    request.message.streams = streams;
  }

  return request;
};

// .ledger_choose()
// .ledger_hash()
// .ledger_index()
Remote.prototype.request_transaction_entry = function (hash) {
  //utils.assert(this.trusted);   // If not trusted, need to check proof, maybe talk packet protocol.

  return (new Request(this, 'transaction_entry'))
    .tx_hash(hash);
};

// DEPRECATED: use request_transaction_entry
Remote.prototype.request_tx = function (hash) {
  var request = new Request(this, 'tx');

  request.message.transaction  = hash;

  return request;
};

Remote.prototype.request_account_info = function (accountID) {
  var request = new Request(this, 'account_info');

  request.message.ident   = UInt160.json_rewrite(accountID);  // DEPRECATED
  request.message.account = UInt160.json_rewrite(accountID);

  return request;
};

// --> account_index: sub_account index (optional)
// --> current: true, for the current ledger.
Remote.prototype.request_account_lines = function (accountID, account_index, current) {
  // XXX Does this require the server to be trusted?
  //utils.assert(this.trusted);

  var request = new Request(this, 'account_lines');

  request.message.account = UInt160.json_rewrite(accountID);

  if (account_index)
    request.message.index   = account_index;

  return request
    .ledger_choose(current);
};

// --> account_index: sub_account index (optional)
// --> current: true, for the current ledger.
Remote.prototype.request_account_offers = function (accountID, account_index, current) {
  var request = new Request(this, 'account_offers');

  request.message.account = UInt160.json_rewrite(accountID);

  if (account_index)
    request.message.index   = account_index;

  return request
    .ledger_choose(current);
};


/*
  account: account,
  ledger_index_min: ledger_index, // optional, defaults to -1 if ledger_index_max is specified.
  ledger_index_max: ledger_index, // optional, defaults to -1 if ledger_index_min is specified.
  binary: boolean,                // optional, defaults to false
  count: boolean,                 // optional, defaults to false
  descending: boolean,            // optional, defaults to false
  offset: integer,                // optional, defaults to 0
  limit: integer                  // optional
*/

Remote.prototype.request_account_tx = function (obj) {
  // XXX Does this require the server to be trusted?
  //utils.assert(this.trusted);

  var request = new Request(this, 'account_tx');

  request.message.account     = obj.account;

  if (false && ledger_min === ledger_max) {
    //request.message.ledger      = ledger_min;
  }
  else {
	if ("undefined" != typeof obj.ledger_index_min)	{request.message.ledger_index_min  = obj.ledger_index_min;}
    if ("undefined" != typeof obj.ledger_index_max)	{request.message.ledger_index_max  = obj.ledger_index_max;}
	if ("undefined" != typeof obj.binary)			{request.message.binary  = obj.binary;}
	if ("undefined" != typeof obj.count)			{request.message.count  = obj.count;}
	if ("undefined" != typeof obj.descending)		{request.message.descending  = obj.descending;}
	if ("undefined" != typeof obj.offset)			{request.message.offset  = obj.offset;}
	if ("undefined" != typeof obj.limit)			{request.message.limit  = obj.limit;}
  }
  return request;
};

Remote.prototype.request_book_offers = function (gets, pays, taker) {
  var request = new Request(this, 'book_offers');

  request.message.taker_gets = {
    currency: Currency.json_rewrite(gets.currency)
  };

  if (request.message.taker_gets.currency !== 'XRP') {
    request.message.taker_gets.issuer = UInt160.json_rewrite(gets.issuer);
  }

  request.message.taker_pays = {
    currency: Currency.json_rewrite(pays.currency)
  };

  if (request.message.taker_pays.currency !== 'XRP') {
    request.message.taker_pays.issuer = UInt160.json_rewrite(pays.issuer);
  }

  request.message.taker = taker ? taker : UInt160.ACCOUNT_ONE;

  return request;
};

Remote.prototype.request_wallet_accounts = function (seed) {
  utils.assert(this.trusted);     // Don't send secrets.

  var request = new Request(this, 'wallet_accounts');

  request.message.seed = seed;

  return request;
};

Remote.prototype.request_sign = function (secret, tx_json) {
  utils.assert(this.trusted);     // Don't send secrets.

  var request = new Request(this, 'sign');

  request.message.secret = secret;
  request.message.tx_json = tx_json;

  return request;
};

// Submit a transaction.
Remote.prototype.request_submit = function () {
  var self  = this;

  var request = new Request(this, 'submit');

  return request;
};

//
// Higher level functions.
//

/**
 * Create a subscribe request with current subscriptions.
 *
 * Other classes can add their own subscriptions to this request by listening to
 * the server_subscribe event.
 *
 * This function will create and return the request, but not submit it.
 */
Remote.prototype._server_prepare_subscribe = function ()
{
  var self  = this;

  var feeds = [ 'ledger', 'server' ];

  if (this._transaction_subs)
    feeds.push('transactions');

  var req = this.request_subscribe(feeds);

  req.on('success', function (message) {
    self._stand_alone       = !!message.stand_alone;
    self._testnet           = !!message.testnet;

    if ("string" === typeof message.random) {
      var rand = message.random.match(/[0-9A-F]{8}/ig);
      while (rand && rand.length)
        sjcl.random.addEntropy(parseInt(rand.pop(), 16));

      self.emit('random', utils.hexToArray(message.random));
    }

    if (message.ledger_hash && message.ledger_index) {
      self._ledger_time           = message.ledger_time;
      self._ledger_hash           = message.ledger_hash;
      self._ledger_current_index  = message.ledger_index+1;

      self.emit('ledger_closed', message);
    }

    // FIXME Use this to estimate fee.
    // XXX When we have multiple server support, most of this should be tracked
    //     by the Server objects and then aggregated/interpreted by Remote.
    self._load_base     = message.load_base || 256;
    self._load_factor   = message.load_factor || 1.0;
    self._fee_ref       = message.fee_ref;
    self._fee_base      = message.fee_base;
    self._reserve_base  = message.reserve_base;
    self._reserve_inc   = message.reserve_inc;

    self.emit('subscribed');
  });

  self.emit('prepare_subscribe', req);

  // XXX Could give error events, maybe even time out.

  return req;
};

// For unit testing: ask the remote to accept the current ledger.
// - To be notified when the ledger is accepted, server_subscribe() then listen to 'ledger_hash' events.
// A good way to be notified of the result of this is:
//    remote.once('ledger_closed', function (ledger_closed, ledger_index) { ... } );
Remote.prototype.ledger_accept = function () {
  if (this._stand_alone || undefined === this._stand_alone)
  {
    var request = new Request(this, 'ledger_accept');

    request
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
  var request = this.request_ledger_entry('account_root');

  return request
    .account_root(account)
    .ledger_choose(current)
    .on('success', function (message) {
        // If the caller also waits for 'success', they might run before this.
        request.emit('account_balance', Amount.from_json(message.node.Balance));
      });
};

// Return a request to emit the owner count.
Remote.prototype.request_owner_count = function (account, current) {
  var request = this.request_ledger_entry('account_root');

  return request
    .account_root(account)
    .ledger_choose(current)
    .on('success', function (message) {
        // If the caller also waits for 'success', they might run before this.
        request.emit('owner_count', message.node.OwnerCount);
      });
};

Remote.prototype.account = function (accountId) {
  accountId = UInt160.json_rewrite(accountId);

  if (!this._accounts[accountId]) {
    var account = new Account(this, accountId);

    if (!account.is_valid()) return account;

    this._accounts[accountId] = account;
  }

  return this._accounts[accountId];
};

Remote.prototype.book = function (currency_gets, issuer_gets,
                                  currency_pays, issuer_pays) {
  var gets = currency_gets;
  if (gets !== 'XRP') gets += '/' + issuer_gets;
  var pays = currency_pays;
  if (pays !== 'XRP') pays += '/' + issuer_pays;

  var key = gets + ":" + pays;

  if (!this._books[key]) {
    var book = new OrderBook(this,
                             currency_gets, issuer_gets,
                             currency_pays, issuer_pays);

    if (!book.is_valid()) return book;

    this._books[key] = book;
  }

  return this._books[key];
}

// Return the next account sequence if possible.
// <-- undefined or Sequence
Remote.prototype.account_seq = function (account, advance) {
  account           = UInt160.json_rewrite(account);
  var account_info  = this.accounts[account];
  var seq;

  if (account_info && account_info.seq)
  {
    seq = account_info.seq;

    if (advance === "ADVANCE") account_info.seq += 1;
    if (advance === "REWIND") account_info.seq -= 1;

    // console.log("cached: %s current=%d next=%d", account, seq, account_info.seq);
  }
  else {
    // console.log("uncached: %s", account);
  }

  return seq;
}

Remote.prototype.set_account_seq = function (account, seq) {
  var account       = UInt160.json_rewrite(account);

  if (!this.accounts[account]) this.accounts[account] = {};

  this.accounts[account].seq = seq;
}

// Return a request to refresh accounts[account].seq.
Remote.prototype.account_seq_cache = function (account, current) {
  var self    = this;
  var request;

  if (!self.accounts[account]) self.accounts[account] = {};

  var account_info = self.accounts[account];

  request = account_info.caching_seq_request;
  if (!request) {
    // console.log("starting: %s", account);
    request = self.request_ledger_entry('account_root')
      .account_root(account)
      .ledger_choose(current)
      .on('success', function (message) {
          delete account_info.caching_seq_request;

          var seq = message.node.Sequence;

          account_info.seq  = seq;

          // console.log("caching: %s %d", account, seq);
          // If the caller also waits for 'success', they might run before this.
          request.emit('success_account_seq_cache', message);
        })
      .on('error', function (message) {
          // console.log("error: %s", account);
          delete account_info.caching_seq_request;

          request.emit('error_account_seq_cache', message);
        });

    account_info.caching_seq_request    = request;
  }

  return request;
};

// Mark an account's root node as dirty.
Remote.prototype.dirty_account_root = function (account) {
  var account       = UInt160.json_rewrite(account);

  delete this.ledgers.current.account_root[account];
};

// Store a secret - allows the Remote to automatically fill out auth information.
Remote.prototype.set_secret = function (account, secret) {
  this.secrets[account] = secret;
};


// Return a request to get a ripple balance.
//
// --> account: String
// --> issuer: String
// --> currency: String
// --> current: bool : true = current ledger
//
// If does not exist: emit('error', 'error' : 'remoteError', 'remote' : { 'error' : 'entryNotFound' })
Remote.prototype.request_ripple_balance = function (account, issuer, currency, current) {
  var request       = this.request_ledger_entry('ripple_state');          // YYY Could be cached per ledger.

  return request
    .ripple_state(account, issuer, currency)
    .ledger_choose(current)
    .on('success', function (message) {
        var node            = message.node;

        var lowLimit        = Amount.from_json(node.LowLimit);
        var highLimit       = Amount.from_json(node.HighLimit);
        // The amount the low account holds of issuer.
        var balance         = Amount.from_json(node.Balance);
        // accountHigh implies: for account: balance is negated, highLimit is the limit set by account.
        var accountHigh     = UInt160.from_json(account).equals(highLimit.issuer());

        request.emit('ripple_state', {
          'account_balance'     : ( accountHigh ? balance.negate() : balance.clone()).parse_issuer(account),
          'peer_balance'        : (!accountHigh ? balance.negate() : balance.clone()).parse_issuer(issuer),

          'account_limit'       : ( accountHigh ? highLimit : lowLimit).clone().parse_issuer(issuer),
          'peer_limit'          : (!accountHigh ? highLimit : lowLimit).clone().parse_issuer(account),

          'account_quality_in'  : ( accountHigh ? node.HighQualityIn : node.LowQualityIn),
          'peer_quality_in'     : (!accountHigh ? node.HighQualityIn : node.LowQualityIn),

          'account_quality_out' : ( accountHigh ? node.HighQualityOut : node.LowQualityOut),
          'peer_quality_out'    : (!accountHigh ? node.HighQualityOut : node.LowQualityOut),
        });
      });
};

Remote.prototype.request_ripple_path_find = function (src_account, dst_account, dst_amount, source_currencies) {
  var self    = this;
  var request = new Request(this, 'ripple_path_find');

  request.message.source_account      = UInt160.json_rewrite(src_account);
  request.message.destination_account = UInt160.json_rewrite(dst_account);
  request.message.destination_amount  = Amount.json_rewrite(dst_amount);

  if (source_currencies) {
    request.message.source_currencies   = source_currencies.map(function (ci) {
      var ci_new  = {};

      if ('issuer' in ci)
        ci_new.issuer   = UInt160.json_rewrite(ci.issuer);

      if ('currency' in ci)
        ci_new.currency = Currency.json_rewrite(ci.currency);

      return ci_new;
    });
  }

  return request;
};

Remote.prototype.request_unl_list = function () {
  return new Request(this, 'unl_list');
};

Remote.prototype.request_unl_add = function (addr, comment) {
  var request = new Request(this, 'unl_add');

  request.message.node    = addr;

  if (comment !== undefined)
    request.message.comment = note;

  return request;
};

// --> node: <domain> | <public_key>
Remote.prototype.request_unl_delete = function (node) {
  var request = new Request(this, 'unl_delete');

  request.message.node = node;

  return request;
};

Remote.prototype.request_peers = function () {
  return new Request(this, 'peers');
};

Remote.prototype.request_connect = function (ip, port) {
  var request = new Request(this, 'connect');

  request.message.ip = ip;

  if (port)
    request.message.port = port;

  return request;
};

Remote.prototype.transaction = function () {
  return new Transaction(this);
};

exports.Remote          = Remote;

// vim:sw=2:sts=2:ts=8:et
