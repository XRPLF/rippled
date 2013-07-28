//
// Configuration for unit tests: to be locally customized as needed.
//

var path        = require("path");
var testconfig  = require("./testconfig.js");

exports.accounts = testconfig.accounts;

exports.server_default  = "alpha";

exports.default_server_config = {
  // Where to find the binary.
  rippled_path: path.resolve(__dirname, "../build/rippled")
};

//
// Configuration for servers.
//
// For testing, you might choose to target a persistent server at alternate ports.
//
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
    'websocket_ssl' : false,
    'local_sequence' : true,
    'local_fee' : true,
    // 'validation_seed' : "shhDFVsmS2GSu5vUyZSPXYfj1r79h",
    // 'validators' : "n9L8LZZCwsdXzKUN9zoVxs4YznYXZ9hEhsQZY7aVpxtFaSceiyDZ beta",
    'local_signing' : false,
    'node_db': 'type=memory'
  }
};

exports.http_servers = {
  // A local test server
  "zed" : {
    "ip" : "127.0.0.1",
    "port" : 8088,
  }
};

// vim:sw=2:sts=2:ts=8:et
