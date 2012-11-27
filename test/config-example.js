//
// Configuration for unit tests: to be locally customized as needed.
//

var path        = require("path");
var testconfig  = require("./testconfig.js");

exports.accounts = testconfig.accounts;

// Where to find the binary.
exports.rippled = path.resolve("build/rippled");

exports.server_default  = "alpha";

// Configuration for servers.
exports.servers = {
  // A local test server.
  "alpha" : {
    'trusted' : true,
    // "peer_ip" : "0.0.0.0",
    // "peer_port" : 51235,
    'rpc_ip' : "0.0.0.0",
    'rpc_port' : 5005,
    'websocket_ip' : "127.0.0.1",
    'websocket_port' : 5006,
    'local_sequence' : true,
    'local_fee' : true,
    // 'validation_seed' : "shhDFVsmS2GSu5vUyZSPXYfj1r79h",
    // 'validators' : "n9L8LZZCwsdXzKUN9zoVxs4YznYXZ9hEhsQZY7aVpxtFaSceiyDZ beta"
  }
};

// vim:sw=2:sts=2:ts=8:et
