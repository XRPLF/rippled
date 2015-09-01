require('babel/register');

var extend = require('extend');
var mocha = require('mocha');
var ripplelib = require('ripple-lib');

// Optionally use a more useful (but noisy) logger
if (process.env.USE_RCONSOLE) {
  require('rconsole');
};

// Stash a reference away to this
extend(ripplelib, ripplelib._DEPRECATED);

var config = ripplelib.config = {
  load: function (newOpts) {
    extend(ripplelib.config, newOpts);
    return config;
  }
}

// camelCase to under_scored API conversion
function attachUnderscored(c) {
  var o = ripplelib[c];

  Object.keys(o.prototype).forEach(function(key) {
    var UPPERCASE = /([A-Z]{1})[a-z]+/g;

    if (!UPPERCASE.test(key)) {
      return;
    }

    var underscored = key.replace(UPPERCASE, function(c) {
      return '_' + c.toLowerCase();
    });

    o.prototype[underscored] = o.prototype[key];
  });
};

[ 'Remote',
  'Request',
  'Transaction',
  'Account',
  'Server'
].forEach(attachUnderscored);

var Remote = ripplelib.Remote;
Remote.from_config = function(obj, trace) {
  var serverConfig = (typeof obj === 'string') ? config.servers[obj] : obj;
  var remote = new Remote(serverConfig, trace);

  function initializeAccount(account) {
    var accountInfo = config.accounts[account];
    if (typeof accountInfo === 'object') {
      if (accountInfo.secret) {
        // Index by nickname
        remote.setSecret(account, accountInfo.secret);
        // Index by account ID
        remote.setSecret(accountInfo.account, accountInfo.secret);
      }
    }
  };

  if (config.accounts) {
    Object.keys(config.accounts).forEach(initializeAccount);
  }

  return remote;
};

var amountParse = ripplelib.Amount.prototype.parse_json;
ripplelib.Amount.prototype.parse_json = function(j) {
  if (typeof j === 'string'/* || typeof j === 'number'*/) {
    /*j = String(j);*/
    if (j.match(/^\s*\d+\.\d+\s*$/)) {
      j = String(Math.floor(parseFloat(j, 10) * 1e6));
    }
  }
  return amountParse.call(this, j);
}

var accountParse = ripplelib.UInt160.prototype.parse_json;
ripplelib.UInt160.prototype.parse_json = function(j) {
  if (config.accounts[j]) {
    j = config.accounts[j].account;
  }
  return accountParse.call(this, j);
}


var oldLoader = mocha.prototype.loadFiles
if (!oldLoader.monkeyPatched) {
  // Gee thanks Mocha ...
  mocha.prototype.loadFiles = function() {
    try {
      oldLoader.apply(this, arguments);
    } catch (e) {
      // Normally mocha just silently bails
      console.error(e.stack);
      // We throw, so mocha doesn't continue trying to run the test suite
      throw e;
    }
  }
  mocha.prototype.loadFiles.monkeyPatched = true;
};

