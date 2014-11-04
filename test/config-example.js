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

var lines = function() {return Array.prototype.slice.call(arguments).join('\n')}

exports.servers = {
  // A local test server.
  "alpha" : {

    // ripple-lib.Remote
    'local_fee' : true,
    'local_sequence' : true,
    'local_signing' : false,
    'trace' : false,
    'trusted' : true,

    'websocket_ip': "127.0.0.1",
    'websocket_port': 5006,
    'websocket_ssl': false,

    // json rpc test
    'rpc_ip' : "127.0.0.1",
    'rpc_port' : 5005,

    // rippled.cfg
    'server' : lines('port_admin_http',
                     'port_admin_ws'),

    'port_admin_http': lines('port = 5005',
                             'ip = 127.0.0.1',
                             'admin = allow',
                             'protocol = http'),

    'port_admin_ws': lines('port = 5006',
                           'ip = 127.0.0.1',
                           'admin = allow',
                           'protocol = ws'),

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