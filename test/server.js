// Manage test servers

// Provide servers
//
// Servers are created in tmp/server/$server
//

console.log("server.js>");

var utils = require("./utils.js");

// var child = require("child");

var serverPath = function(name) {
    return "tmp/server/" + name;
};

var makeBase = function(name, done) {
    var	path	= serverPath(name);

    console.log("start> %s: %s", name, path);

    // Remove the existing dir.
    utils.resetPath(path, parseInt('0777', 8), done);

    console.log("start< %s", name);
};

var start = function(name, done) {
    makeBase(name, done);
};

exports.start = start;

console.log("server.js<");
// vim:ts=4
