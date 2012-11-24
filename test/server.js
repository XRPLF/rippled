// Create and stop test servers.
//
// Usage:
// s = new Server(name, config)
// s.verbose()  : optional
//  .start()
//      'started'
//
// s.stop()     : stops server is started.
//   'stopped'
//

// Provide servers
//
// Servers are created in tmp/server/$server
//

var buster        = require("buster");
var child         = require("child_process");
var fs            = require("fs");
var path          = require("path");
var util          = require("util");
var EventEmitter  = require('events').EventEmitter;

var config        = require("./config.js");
var nodeutils     = require("../src/js/nodeutils.js");

// Create a server object
var Server = function (name, config, verbose) {
  this.name     = name;
  this.config   = config;
  this.started  = false;
  this.quiet    = !verbose;
};

Server.prototype  = new EventEmitter;

Server.from_config = function (name, verbose) {
  return new Server(name, config.servers[name], verbose);
};

Server.prototype.on = function (e, c) {
  EventEmitter.prototype.on.call(this, e, c);

  return this;
};

Server.prototype.once = function (e, c) {
  EventEmitter.prototype.once.call(this, e, c);

  return this;
};

Server.prototype.serverPath = function() {
  return path.resolve("tmp/server", this.name);
};

Server.prototype.configPath = function() {
  return path.join(this.serverPath(), "rippled.cfg");
};

// Write a server's rippled.cfg.
Server.prototype._writeConfig = function(done) {
  var self  = this;

  fs.writeFile(
    this.configPath(),
    Object.keys(this.config).map(function(o) {
        return util.format("[%s]\n%s\n", o, self.config[o]);
      }).join(""),
    'utf8', done);
};

// Spawn the server.
Server.prototype._serverSpawnSync = function() {
  var self  = this;

  var args  = [
      "-a",
      "-v",
      "--conf=rippled.cfg"
    ];

  // Spawn in standalone mode for now.
  this.child = child.spawn(
    config.rippled,
    args,
    {
      cwd: this.serverPath(),
      env: process.env,
      stdio: this.quiet ? 'ignore' : 'inherit'
    });

  if (!this.quiet)
    console.log("server: start %s: %s --conf=%s",
      this.child.pid, config.rippled, args.join(" "), this.configPath());

  // By default, just log exits.
  this.child.on('exit', function(code, signal) {
      // If could not exec: code=127, signal=null
      // If regular exit: code=0, signal=null
      if (!self.quiet) console.log("server: spawn: server exited code=%s: signal=%s", code, signal);
    });
};

// Prepare server's working directory.
Server.prototype._makeBase = function (done) {
  var path  = this.serverPath();
  var self  = this;

  // Reset the server directory, build it if needed.
  nodeutils.resetPath(path, '0777', function (e) {
      if (e) {
        throw e;
      }
      else {
        self._writeConfig(done);
      }
    });
};

Server.prototype.verbose = function () {
  this.quiet  = false;

  return this;
};

// Create a standalone server.
// Prepare the working directory and spawn the server.
Server.prototype.start = function () {
  var self      = this;

  if (!this.quiet) console.log("server: start: %s: %s", this.name, JSON.stringify(this.config));

  this._makeBase(function (e) {
      if (e) {
        throw e;
      }
      else {
        self._serverSpawnSync();
        self.emit('started');
      }
    });

  return this;
};

// Stop a standalone server.
Server.prototype.stop = function () {
  var self  = this;

  if (this.child) {
    // Update the on exit to invoke done.
    this.child.on('exit', function (code, signal) {

        if (!self.quiet) console.log("server: stop: server exited");

        self.emit('stopped');
        delete this.child;
      });

    this.child.kill();
  }
  else
  {
    buster.log("server: stop: can't stop");
  }

  return this;
};

exports.Server = Server;

// vim:sw=2:sts=2:ts=8:et
