//
// Access to the Ripple network via multiple untrusted servers or a single trusted server.
//
// Overview:
//  Network configuration.
//  Can leverage local storage to remember network configuration
//  Aquires the network
// events:
//   online
//   offline
//

var remote  = require("./remote.js");

var opts_default = {
    DEFAULT_VALIDATORS_SITE : "redstem.com",

    ips = {
    }
};

//
// opts : {
//  cache : undefined || {
//    get : function () { return cached_value; },
//    set : function (value) { cached_value = value; },
//  },
//
//  // Where to get validators.txt if needed.
//  DEFAULT_VALIDATORS_SITE : _domain_,
//
//  // Validator.txt to use.
//  validators : _txt_,
// }
//

var Network = function (opts) {

};

// Set the network configuration.
Network.protocol.configure = function () {

};

// Target state: connectted
Network.protocol.start = function () {

};

// Target state: disconnect
Network.protocol.stop = function () {

};

exports.Network	= Network;

// vim:sw=2:sts=2:ts=8:et
