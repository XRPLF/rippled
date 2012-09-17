// Manage test servers

// Provide servers
//
// Servers are created in tmp/server/$server
//

console.log("server.js>");

var config = require("./config.js");
var utils = require("./utils.js");

var fs = require("fs");
var path = require("path");
var util = require("util");
// var child = require("child");

var serverPath = function(name) {
    return "tmp/server/" + name;
};

// Return a server's newcoind.cfg as string.
var serverConfig = function(name) {
	var	cfg	= config.servers[name];

	return Object.keys(cfg).map(function (o) {
		return util.format("[%s]\n%s\n", o, cfg[o]);
		}).join("");
};

// Write a server's newcoind.cfg.
var writeConfig = function(name, done) {
	fs.writeFile(path.join(serverPath(name), "newcoind.cfg"), serverConfig(name), 'utf8', done);
};

var makeBase = function(name, done) {
    var	path	= serverPath(name);

    console.log("start> %s: %s", name, path);

    // Reset the server directory, build it if needed.
    utils.resetPath(path, '0777', function (e) {
			if (e) {
				throw e;
			}
			else {
				writeConfig(name, done);
			}
		});

    console.log("start< %s", name);
};

var start = function(name, done) {
    makeBase(name, done);
};

exports.start = start;

console.log("server.js<");
// vim:ts=4
