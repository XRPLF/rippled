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

var child        = require("child_process");
var fs           = require("fs");
var path         = require("path");
var util         = require("util");
var assert       = require('assert');
var EventEmitter = require('events').EventEmitter;
var nodeutils     = require("./nodeutils");

// Create a server object
function Server(name, config, verbose) {
  this.name     = name;
  this.config   = config;
  this.started  = false;
  this.quiet    = !verbose;
  this.stopping = false;

  var nodejs_version = process.version.match(/^v(\d+)+\.(\d+)\.(\d+)$/).slice(1,4);
  var wanted_version = [ 0, 8, 18 ];

  while (wanted_version.length && nodejs_version.length && nodejs_version[0] == wanted_version[0])
  {
    nodejs_version.shift();
    wanted_version.shift();
  }

  var sgn = !nodejs_version.length && !wanted_version.length
            ? 0
            : nodejs_version.length
              ? nodejs_version[0] - wanted_version[0]
              : -1;

  if (sgn < 0) {
    console.log("\n*** Node.js version is too low.");
    throw "Nodejs version is too low.";
  }
};

util.inherits(Server, EventEmitter);

Server.from_config = function (name, config, verbose) {
  return new Server(name, config, verbose);
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

  var options = {
    cwd: this.serverPath(),
    env: process.env,
    stdio: this.quiet ? 'ignore' : 'inherit'
  };

  // Spawn in standalone mode for now.
  this.child = child.spawn(this.config.rippled_path, args, options);

  if (!this.quiet)
    console.log("server: start %s: %s --conf=%s",
                this.child.pid,
                this.config.rippled_path,
                args.join(" "),
                this.configPath());

  // By default, just log exits.
  this.child.on('exit', function(code, signal) {
    if (!self.quiet) console.log("server: spawn: server exited code=%s: signal=%s", code, signal);

    self.emit('exited');

    // If could not exec: code=127, signal=null
    // If regular exit: code=0, signal=null
    // Fail the test if the server has not called "stop".
    assert(self.stopping, 'Server died with signal '+signal);
  });
};

// Prepare server's working directory.
Server.prototype._makeBase = function (done) {
  var path  = this.serverPath();
  var self  = this;

  // Reset the server directory, build it if needed.
  nodeutils.resetPath(path, '0777', function (e) {
    if (e) throw e;
    self._writeConfig(done);
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
    if (e) throw e;
    self._serverSpawnSync();
    self.emit('started');
  });

  return this;
};

// Stop a standalone server.
Server.prototype.stop = function () {
  var self  = this;

  self.stopping = true;

  if (!this.child) {
    console.log("server: stop: can't stop");
    return;
  }

  // Update the on exit to invoke done.
  this.child.on('exit', function (code, signal) {
    if (!self.quiet) console.log("server: stop: server exited");
    self.emit('stopped');
    delete self.child;
  });

  this.child.kill();

  return this;
};

exports.Server = Server;

// vim:sw=2:sts=2:ts=8:et
