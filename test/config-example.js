//
// Configuration for unit tests: to be locally customized as needed.
//

var extend      = require('extend');
var path        = require("path");
var extend      = require('extend');
var testconfig  = require("./testconfig.js");

//
// Helpers
//

function lines () {
  return Array.prototype.slice.call(arguments).join('\n');
};

function for_each_item (o, f) {
  for (var k in o) {
    if (o.hasOwnProperty(k)) {
      f(k, o[k], o);
    }
  }
};

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

    // ripple-lib.Remote
    'local_fee' : true,
    'local_sequence' : true,
    'local_signing' : false,
    'trace' : false,
    'trusted' : true,

    'servers' : ['ws://127.0.0.1:5006'],
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
                             'admin = 127.0.0.1',
                             'protocol = http'),

    'port_admin_ws': lines('port = 5006',
                           'ip = 127.0.0.1',
                           'admin = 127.0.0.1',
                           'protocol = ws'),

    'node_db': lines('type=memory', 'path=integration'),

    features: lines('MultiSign')
  },

  'uniport_tests' : {
    // rippled.cfg
    'node_db': lines('type=memory', 'path=integration'),

    // We let testutils.build_setup connect normally, and use the Remote to
    // determine when the server is ready, so we must configure it, even though
    // we don't otherwise use it in the test.
    'websocket_ip': "127.0.0.1",
    'websocket_port': 6432,
    'websocket_ssl': false,
    'servers' : ['ws://127.0.0.1:6432'],
    'trusted' : true,

  }
};

exports.servers.debug = extend({
  no_server: true,
  debug_logfile: "debug.log"
}, exports.servers.alpha);

exports.uniport_test_ports = {
    'port_admin_http':
        {'admin': '127.0.0.1', 'port': 6434, 'protocol': 'http'},
    'port_admin_http_and_https':
        {'admin': '127.0.0.1', 'port': 6437, 'protocol': 'http,https'},
    'port_admin_https':
        {'admin': '127.0.0.1', 'port': 6435, 'protocol': 'https'},
    'port_admin_ws':
        {'admin': '127.0.0.1', 'port': 6432, 'protocol': 'ws'},
    'port_admin_ws_and_wss':
        {'admin': '127.0.0.1', 'port': 6436, 'protocol': 'ws,wss'},
    'port_admin_wss':
        {'admin': '127.0.0.1', 'port': 6433, 'protocol': 'wss'},

    'port_http':
        {'admin': '', 'port': 6440, 'protocol': 'http'},
    'port_http_and_https':
        {'admin': '', 'port': 6443, 'protocol': 'http,https'},
    'port_https':
        {'admin': '', 'port': 6441, 'protocol': 'https'},
    'port_https_and_http_and_peer':
        {'admin': '', 'port': 6450, 'protocol': 'https,peer,http'},

    'port_passworded_admin_http':
        {'admin': '127.0.0.1', 'admin_password': 'p', 'admin_user': 'u',
         'port': 6446, 'protocol': 'http'},
    'port_passworded_admin_http_and_https':
        {'admin': '127.0.0.1', 'admin_password': 'p', 'admin_user': 'u',
         'port': 6449, 'protocol': 'http,https'},
    'port_passworded_admin_https':
        {'admin': '127.0.0.1', 'admin_password': 'p', 'admin_user': 'u',
         'port': 6447, 'protocol': 'https'},
    'port_passworded_admin_ws':
        {'admin': '127.0.0.1', 'admin_password': 'p', 'admin_user': 'u',
         'port': 6444, 'protocol': 'ws'},
    'port_passworded_admin_ws_and_wss':
        {'admin': '127.0.0.1', 'admin_password': 'p', 'admin_user': 'u',
         'port': 6448, 'protocol': 'ws,wss'},
    'port_passworded_admin_wss':
        {'admin': '127.0.0.1', 'admin_password': 'p', 'admin_user': 'u',
         'port': 6445, 'protocol': 'wss'},

    'port_ws':
        {'admin': '', 'port': 6438, 'protocol': 'ws'},
    'port_ws_and_wss':
        {'admin': '', 'port': 6442, 'protocol': 'ws,wss'},
    'port_wss':
        {'admin': '', 'port': 6439, 'protocol': 'wss'}
};

(function() {
  var server_config = exports.servers.uniport_tests;
  var test_ports = exports.uniport_test_ports;

  // [server]
  server_config.server = Object.keys(test_ports).join('\n');

  // [port_*]
  for_each_item(test_ports, function(port_name, options) {
    var opt_line = ['ip=127.0.0.1'];
    for_each_item(options, function(k, v) {opt_line.push(k+'='+v);});
    server_config[port_name] = opt_line.join('\n');
  });
}());

exports.http_servers = {
  // A local test server
  "zed" : {
    "ip" : "127.0.0.1",
    "port" : 8088,
  }
};

// vim:sw=2:sts=2:ts=8:et
