// Manage test servers
//
// YYY Would be nice to be able to hide server output.
//

// Provide servers
//
// Servers are created in tmp/server/$server
//

var config    = require("./config.js");
var nodeutils = require("../js/nodeutils.js");

var fs	      = require("fs");
var path      = require("path");
var util      = require("util");
var child     = require("child_process");

var servers = {};

// Create a server object
var Server = function(name) {
    this.name = name;
};

// Return a server's newcoind.cfg as string.
Server.prototype.configContent = function() {
    var	cfg = config.servers[this.name];

    return Object.keys(cfg).map(function(o) {
	return util.format("[%s]\n%s\n", o, cfg[o]);
	}).join("");
};

Server.prototype.serverPath = function() {
    return "tmp/server/" + this.name;
};

Server.prototype.configPath = function() {
    return path.join(this.serverPath(), "newcoind.cfg");
};

// Write a server's newcoind.cfg.
Server.prototype.writeConfig = function(done) {
    fs.writeFile(this.configPath(), this.configContent(), 'utf8', done);
};

// Spawn the server.
Server.prototype.serverSpawnSync = function() {
    // Spawn in standalone mode for now.
    this.child = child.spawn(
	config.newcoind,
	[
	    "-a",
	    "-v",
	    "--conf=newcoind.cfg"
	],
	{
	    cwd: this.serverPath(),
	    env: process.env,
	    stdio: 'inherit'
	});

    console.log("server: start %s: %s -a --conf=%s", this.child.pid, config.newcoind, this.configPath());

    // By default, just log exits.
    this.child.on('exit', function(code, signal) {
	// If could not exec: code=127, signal=null
	// If regular exit: code=0, signal=null
	console.log("server: spawn: server exited code=%s: signal=%s", code, signal);
	});

};

// Prepare server's working directory.
Server.prototype.makeBase = function(done) {
    var	path	= this.serverPath();
    var self	= this;

    // Reset the server directory, build it if needed.
    nodeutils.resetPath(path, '0777', function(e) {
	    if (e) {
		throw e;
	    }
	    else {
		self.writeConfig(done);
	    }
	});
};

// Create a standalone server.
// Prepare the working directory and spawn the server.
Server.prototype.start = function(done) {
    var self	= this;

    this.makeBase(function(e) {
	    if (e) {
		throw e;
	    }
	    else {
		self.serverSpawnSync();
		done();
	    }
	});
};

// Stop a standalone server.
Server.prototype.stop = function(done) {
    if (this.child) {
	// Update the on exit to invoke done.
	this.child.on('exit', function(code, signal) {
	    console.log("server: stop: server exited");
	    done();
	    });
	this.child.kill();
    }
    else
    {
	console.log("server: stop: no such server");
	done('noSuchServer');	
    }
};

// Start the named server.
exports.start = function(name, done) {
    if (servers[name])
    {
	console.log("server: start: server already started.");
    }
    else
    {
	var server = new Server(name);

	servers[name] = server;

	console.log("server: start: %s", JSON.stringify(server));

	server.start(done);
    }
};

// Delete the named server.
exports.stop = function(name, done) {
    console.log("server: stop: %s of %s", name, Object.keys(servers).toString());

    var server	= servers[name];
    if (server) {
	server.stop(done);
	delete servers[name];
    }
};

exports.Server = Server;

// vim:sw=2:sts=2:ts=8
