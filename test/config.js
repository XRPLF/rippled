//
// Configuration for unit tests
//

var path = require("path");

// Where to find the binary.
exports.newcoind = path.join(process.cwd(), "newcoind");

// Configuration for servers.
exports.servers = {
	alpha : {
		// "peer_ip" : "0.0.0.0",
		// "peer_port" : 51235,
		"rpc_ip" : "0.0.0.0",
		"rpc_port" : 5005,
		"websocket_ip" : "127.0.0.1",
		"websocket_port" : 6005,
		"validation_seed" : "shhDFVsmS2GSu5vUyZSPXYfj1r79h",
		"validators" : "n9L8LZZCwsdXzKUN9zoVxs4YznYXZ9hEhsQZY7aVpxtFaSceiyDZ beta"
	}
};
// vim:ts=4
