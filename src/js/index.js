exports.Remote = require('./remote').Remote;
exports.Amount = require('./amount').Amount;
exports.UInt160 = require('./amount').UInt160;
exports.Seed = require('./amount').Seed;

// Important: We do not guarantee any specific version of SJCL or for any
// specific features to be included. The version and configuration may change at
// any time without warning.
//
// However, for programs that are tied to a specific version of ripple.js like
// the official client, it makes sense to expose the SJCL instance so we don't
// have to include it twice.
exports.sjcl = require('../../build/sjcl');
