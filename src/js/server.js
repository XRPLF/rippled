var EventEmitter  = require('events').EventEmitter;

var utils         = require('./utils');

var Server = function (remote, cfg)
{
  if ("object" !== typeof cfg || "string" !== typeof cfg.url) {
    throw new Error("Invalid server configuration.");
  }

  this._remote = remote;
  this._cfg = cfg;

  this._ws = null;
  this._connected = false;
  this._should_connect = false;
  this._state = null;

  this._id = 0;
  this._retry = 0;

  this._requests = [];

  this.on('message', this._handle_message.bind(this));
  this.on('response_subscribe', this._handle_response_subscribe.bind(this));
};

Server.prototype = new EventEmitter;

/**
 * Server states that we will treat as the server being online.
 *
 * Our requirements are that the server can process transactions and notify
 * us of changes.
 */
Server.online_states = [
  'proposing',
  'validating',
  'full'
];

Server.prototype.connect = function ()
{
  var self = this;

  // We don't connect if we believe we're already connected. This means we have
  // recently received a message from the server and the WebSocket has not
  // reported any issues either. If we do fail to ping or the connection drops,
  // we will automatically reconnect.
  if (this._connected === true) return;

  if (this._remote.trace) console.log("server: connect: %s", this._cfg.url);

  // Ensure any existing socket is given the command to close first.
  if (this._ws) this._ws.close();

  var WebSocket = require('ws');
  var ws = this._ws = new WebSocket(this._cfg.url);

  this._should_connect = true;

  self.emit('connecting');

  ws.onopen = function () {
    // If we are no longer the active socket, simply ignore any event
    if (ws !== self._ws) return;

    self.emit('socket_open');

    // Subscribe to events
    var request = self._remote._server_prepare_subscribe();
    self.request(request);
  };

  ws.onerror = function (e) {
    // If we are no longer the active socket, simply ignore any event
    if (ws !== self._ws) return;

    if (self._remote.trace) console.log("server: onerror: %s", e.data || e);

    // Most connection errors for WebSockets are conveyed as "close" events with
    // code 1006. This is done for security purposes and therefore unlikely to
    // ever change.

    // This means that this handler is hardly ever called in practice. If it is,
    // it probably means the server's WebSocket implementation is corrupt, or
    // the connection is somehow producing corrupt data.

    // Most WebSocket applications simply log and ignore this error. Once we
    // support for multiple servers, we may consider doing something like
    // lowering this server's quality score.

    // However, in Node.js this event may be triggered instead of the close
    // event, so we need to handle it.
    handleConnectionClose();
  };

  // Failure to open.
  ws.onclose = function () {
    // If we are no longer the active socket, simply ignore any event
    if (ws !== self._ws) return;

    if (self._remote.trace) console.log("server: onclose: %s", ws.readyState);

    handleConnectionClose();
  };

  function handleConnectionClose()
  {
    self.emit('socket_close');
    self._set_state('offline');

    // Prevent additional events from this socket
    ws.onopen = ws.onerror = ws.onclose = ws.onmessage = function () {};

    // Should we be connected?
    if (!self._should_connect) return;

    // Delay and retry.
    self._retry      += 1;
    self._retry_timer = setTimeout(function () {
      if (self._remote.trace) console.log("server: retry");

      if (!self._should_connect) return;
      self.connect();
    }, self._retry < 40
        ? 1000/20           // First, for 2 seconds: 20 times per second
        : self._retry < 40+60
          ? 1000            // Then, for 1 minute: once per second
          : self._retry < 40+60+60
            ? 10*1000       // Then, for 10 minutes: once every 10 seconds
            : 30*1000);     // Then: once every 30 seconds
  }

  ws.onmessage = function (msg) {
    self.emit('message', msg.data);
  };
};

Server.prototype.disconnect = function ()
{
  this._should_connect = false;
  this._set_state('offline');

  if (this.ws) {
    this.ws.close();
  }
};

/**
 * Submit a Request object to this server.
 */
Server.prototype.request = function (request)
{
  var self  = this;

  // Only bother if we are still connected.
  if (self._ws) {
    request.message.id = self._id;

    self._requests[request.message.id] = request;

    // Advance message ID
    self._id++;

    if (self._state === "online" ||
        (request.message.command === "subscribe" && self._ws.readyState === 1)) {
      if (self._remote.trace) {
        utils.logObject("server: request: %s", request.message);
      }

      self._ws.send(JSON.stringify(request.message));
    } else {
      // XXX There are many ways to make self smarter.
      self.once('connect', function () {
        if (self._remote.trace) {
          utils.logObject("server: request: %s", request.message);
        }
        self._ws.send(JSON.stringify(request.message));
      });
    }
  } else {
    if (self._remote.trace) {
      utils.logObject("server: request: DROPPING: %s", request.message);
    }
  }
};

Server.prototype._set_state = function (state) {
  if (state !== this._state) {
    this._state = state;

    this.emit('state', state);
    if (state === 'online') {
      this.emit('connect');
    } else if (state === 'offline') {
      this.emit('disconnect');
    }
  }
};

Server.prototype._handle_message = function (json) {
  var self = this;

  var message = JSON.parse(json);

  if (message.type === 'response') {
    // A response to a request.
    var request = self._requests[message.id];

    if (!request) {
      if (self._remote.trace) utils.logObject("server: UNEXPECTED: %s", message);
    } else if ('success' === message.status) {
      if (self._remote.trace) utils.logObject("server: response: %s", message);

      request.emit('success', message.result);
      self.emit('response_'+request.message.command, message.result, request, message);
      self._remote.emit('response_'+request.message.command, message.result, request, message);
    } else if (message.error) {
      if (self._remote.trace) utils.logObject("server: error: %s", message);

      request.emit('error', {
        'error'         : 'remoteError',
        'error_message' : 'Remote reported an error.',
        'remote'        : message
      });
    }
  } else if (message.type === 'serverStatus') {
    // This message is only received when online. As we are connected, it is the definative final state.
    self._set_state(
      Server.online_states.indexOf(message.server_status) !== -1
        ? 'online'
        : 'offline');
  }
};

Server.prototype._handle_response_subscribe = function (message)
{
  var self = this;

  self._server_status = message.server_status;

  if (Server.online_states.indexOf(message.server_status) !== -1) {
    self._set_state('online');
  }
};

exports.Server = Server;

// vim:sw=2:sts=2:ts=8:et
