//
// Configuration for unit tests
//

var path = require("path");

// Where to find the binary.
exports.rippled = path.resolve("build/rippled");

exports.server_default	= "alpha";

// Configuration for servers.
exports.servers = {
  // A local test server.
  "alpha" : {
    'trusted' : true,
    'no_server' : true,
    // "peer_ip" : "0.0.0.0",
    // "peer_port" : 51235,
    'rpc_ip' : "0.0.0.0",
    'rpc_port' : 5005,
    'websocket_ip' : "127.0.0.1",
    'websocket_port' : 5006,
    'local_sequence' : true,
    // 'validation_seed' : "shhDFVsmS2GSu5vUyZSPXYfj1r79h",
    // 'validators' : "n9L8LZZCwsdXzKUN9zoVxs4YznYXZ9hEhsQZY7aVpxtFaSceiyDZ beta"
  }
};

// Configuration for test accounts.
exports.accounts = {
  // Users
  "alice" : {
    'account' : "rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn",
    'secret' : "alice",
  },
  "bob" : {
    'account' : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
    'secret' : "bob",
  },
  "carol" : {
    'account' : "rH4KEcG9dEwGwpn6AyoWK9cZPLL4RLSmWW",
    'secret' : "carol",
  },

  // Nexuses
  "bitstamp" : {
    'account' : "r4jKmc2nQb5yEU6eycefiNKGHTU5NQJASx",
    'secret' : "bitstamp",
  },
  "mtgox" : {
    'account' : "rGihwhaqU8g7ahwAvTq6iX5rvsfcbgZw6v",
    'secret' : "mtgox",
  },

  // Merchants
  "amazon" : {
    'account' : "rhheXqX7bDnXePJeMHhubDDvw2uUTtenPd",
    'secret' : "amazon",
  },

  // Master account
  "root" : {
    'account' : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    'secret' : "masterpassphrase",
  },
};

// vim:sw=2:sts=2:ts=8
