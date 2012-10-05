//
// Configuration for unit tests
//

var path = require("path");

// Where to find the binary.
exports.newcoind = path.join(process.cwd(), "newcoind");

// Configuration for servers.
exports.servers = {
	// A local test server.
	'alpha' : {
		'trusted' : true,
		// "peer_ip" : "0.0.0.0",
		// "peer_port" : 51235,
		'rpc_ip' : "0.0.0.0",
		'rpc_port' : 5005,
		'websocket_ip' : "127.0.0.1",
		'websocket_port' : 6005,
		'validation_seed' : "shhDFVsmS2GSu5vUyZSPXYfj1r79h",
		'validators' : "n9L8LZZCwsdXzKUN9zoVxs4YznYXZ9hEhsQZY7aVpxtFaSceiyDZ beta"
	}
};

// Configuration for test accounts.
exports.accounts = {
	// Users
	'alice' : {
		'account' : 'iG1QQv2nh2gi7RCZ1P8YYcBUKCCN633jCn',
		'passphrase' : 'alice',
	},
	'bob' : {
		'account' : 'iPMh7Pr9ct699rZUTWaytJUoHcJ7cgyzrK',
		'passphrase' : 'bob',
	},
	'carol' : {
		'account' : 'iH4KEcG9dEwGwpn6AyoWK9cZPLL4RLSmWW',
		'passphrase' : 'carol',
	},

	// Nexuses
	'bitstamp' : {
		'account' : 'i4jKmc2nQb5yEU6eycefrNKGHTU5NQJASx',
		'passphrase' : 'bitstamp',
	},
	'mtgox' : {
		'account' : 'iGrhwhaqU8g7ahwAvTq6rX5ivsfcbgZw6v',
		'passphrase' : 'mtgox',
	},

	// Merchants
	'amazon' : {
		'account' : 'ihheXqX7bDnXePJeMHhubDDvw2uUTtenPd',
		'passphrase' : 'amazon',
	},

	// Master account
	'root' : {
		'account' : 'iHb9CJAWyB4ij91VRWn96DkukG4bwdtyTh',
		'passphrase' : 'masterpassphrase',
	},
};

// vim:ts=4
