// Manage test servers

// Provide servers
//
// Servers are created in tmp/server/$server
//

var config = require("./config.js");
var utils = require("./utils.js");

var fs = require("fs");
var path = require("path");
var util = require("util");
var child = require("child_process");

var servers = {};

var serverPath = function(name) {
    return "tmp/server/" + name;
};

// Return a server's newcoind.cfg as string.
var configContent = function(name) {
	var	cfg	= config.servers[name];

	return Object.keys(cfg).map(function (o) {
		return util.format("[%s]\n%s\n", o, cfg[o]);
		}).join("");
};

var configPath = function(name) {
	return path.join(serverPath(name), "newcoind.cfg");

};

// Write a server's newcoind.cfg.
var writeConfig = function(name, done) {
	fs.writeFile(configPath(name), configContent(name), 'utf8', done);
};

var serverSpawnSync = function(name) {
	// Spawn in standalone mode for now.
	var server = child.spawn(
		config.newcoind,
		[
			"-a",
			"--conf=" + configPath(name)
		],
		{
			env : process.env,
			stdio : 'inherit'
		});

	servers[name] = server;
	console.log("server: %s: %s -a --conf=%s", server.pid, config.newcoind, configPath(name));
	console.log("sever: start: servers = %s", Object.keys(servers).toString());

	server.on('exit', function (code, signal) {
		// If could not exec: code=127, signal=null
		// If regular exit: code=0, signal=null
		console.log("sever: spawn: server exited code=%s: signal=%s", code, signal);
		delete servers[name];
		});

};

var makeBase = function(name, done) {
    var	path	= serverPath(name);

    // Reset the server directory, build it if needed.
    utils.resetPath(path, '0777', function (e) {
			if (e) {
				throw e;
			}
			else {
				writeConfig(name, done);
			}
		});
};

// Prepare the working directory and spawn the server.
exports.start = function(name, done) {
    makeBase(name, function (e) {
			if (e) {
				throw e;
			}
			else {
				serverSpawnSync(name);
				done();
			}
		});
};

exports.stop = function(name, done) {
	console.log("sever: stop: servers = %s", Object.keys(servers).toString());
	var server	= servers[name];

	if (server) {
		server.on('exit', function (code, signal) {
			console.log("sever: stop: server exited");
			delete servers[name];
			done();
			});
		server.kill();
	}
	else
	{
		console.log("sever: stop: no such server");
		done();	
	}
};

// vim:ts=4
