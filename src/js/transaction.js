//
// Transactions
//
//  Construction:
//    remote.transaction()  // Build a transaction object.
//     .offer_create(...)   // Set major parameters.
//     .set_flags()         // Set optional parameters.
//     .on()                // Register for events.
//     .submit();           // Send to network.
//
//  Events:
// 'success' : Transaction submitted without error.
// 'error' : Error submitting transaction.
// 'proposed' : Advisory proposed status transaction.
// - A client should expect 0 to multiple results.
// - Might not get back. The remote might just forward the transaction.
// - A success could be reverted in final.
// - local error: other remotes might like it.
// - malformed error: local server thought it was malformed.
// - The client should only trust this when talking to a trusted server.
// 'final' : Final status of transaction.
// - Only expect a final from dishonest servers after a tesSUCCESS or ter*.
// 'lost' : Gave up looking for on ledger_closed.
// 'pending' : Transaction was not found on ledger_closed.
// 'state' : Follow the state of a transaction.
//    'client_submitted'     - Sent to remote
//     |- 'remoteError'      - Remote rejected transaction.
//      \- 'client_proposed' - Remote provisionally accepted transaction.
//       |- 'client_missing' - Transaction has not appeared in ledger as expected.
//       | |\- 'client_lost' - No longer monitoring missing transaction.
//       |/
//       |- 'tesSUCCESS'     - Transaction in ledger as expected.
//       |- 'ter...'         - Transaction failed.
//       \- 'tec...'         - Transaction claimed fee only.
//
// Notes:
// - All transactions including those with local and malformed errors may be
//   forwarded anyway.
// - A malicous server can:
//   - give any proposed result.
//     - it may declare something correct as incorrect or something correct as incorrect.
//     - it may not communicate with the rest of the network.
//   - may or may not forward.
//

var sjcl             = require('../../build/sjcl');

var Amount           = require('./amount').Amount;
var Currency         = require('./amount').Currency;
var UInt160          = require('./amount').UInt160;
var Seed             = require('./seed').Seed;
var EventEmitter     = require('events').EventEmitter;
var SerializedObject = require('./serializedobject').SerializedObject;

var config           = require('./config');

var SUBMIT_MISSING  = 4;    // Report missing.
var SUBMIT_LOST     = 8;    // Give up tracking.

// A class to implement transactions.
// - Collects parameters
// - Allow event listeners to be attached to determine the outcome.
var Transaction = function (remote) {
  // YYY Make private as many variables as possible.
  var self  = this;

  this.callback     = undefined;
  this.remote       = remote;
  this._secret      = undefined;
  this._build_path  = false;
  this.tx_json      = {                 // Transaction data.
    'Flags' : 0,                        // XXX Would be nice if server did not require this.
  };
  this.hash         = undefined;
  this.submit_index = undefined;        // ledger_current_index was this when transaction was submited.
  this.state        = undefined;        // Under construction.

  this.on('success', function (message) {
      if (message.engine_result) {
        self.hash       = message.tx_json.hash;

        self.set_state('client_proposed');

        self.emit('proposed', {
            'tx_json'         : message.tx_json,
            'result'          : message.engine_result,
            'result_code'     : message.engine_result_code,
            'result_message'  : message.engine_result_message,
            'rejected'        : self.isRejected(message.engine_result_code),      // If server is honest, don't expect a final if rejected.
          });
      }
    });

  this.on('error', function (message) {
        // Might want to give more detailed information.
        self.set_state('remoteError');
    });
};

Transaction.prototype  = new EventEmitter;

// XXX This needs to be determined from the network.
Transaction.fees = {
  'default'         : Amount.from_json("10"),
  'nickname_create' : Amount.from_json("1000"),
  'offer'           : Amount.from_json("10"),
};

Transaction.flags = {
  'OfferCreate' : {
    'Passive'                 : 0x00010000,
  },

  'Payment' : {
    'NoRippleDirect'          : 0x00010000,
    'PartialPayment'          : 0x00020000,
    'LimitQuality'            : 0x00040000,
  },
};

Transaction.formats = require('./binformat').tx;

Transaction.HASH_SIGN         = 0x53545800;
Transaction.HASH_SIGN_TESTNET = 0x73747800;

Transaction.prototype.consts = {
  'telLOCAL_ERROR'  : -399,
  'temMALFORMED'    : -299,
  'tefFAILURE'      : -199,
  'terRETRY'        : -99,
  'tesSUCCESS'      : 0,
  'tecCLAIMED'      : 100,
};

Transaction.prototype.isTelLocal = function (ter) {
  return ter >= this.consts.telLOCAL_ERROR && ter < this.consts.temMALFORMED;
};

Transaction.prototype.isTemMalformed = function (ter) {
  return ter >= this.consts.temMALFORMED && ter < this.consts.tefFAILURE;
};

Transaction.prototype.isTefFailure = function (ter) {
  return ter >= this.consts.tefFAILURE && ter < this.consts.terRETRY;
};

Transaction.prototype.isTerRetry = function (ter) {
  return ter >= this.consts.terRETRY && ter < this.consts.tesSUCCESS;
};

Transaction.prototype.isTepSuccess = function (ter) {
  return ter >= this.consts.tesSUCCESS;
};

Transaction.prototype.isTecClaimed = function (ter) {
  return ter >= this.consts.tecCLAIMED;
};

Transaction.prototype.isRejected = function (ter) {
  return this.isTelLocal(ter) || this.isTemMalformed(ter) || this.isTefFailure(ter);
};

Transaction.prototype.set_state = function (state) {
  if (this.state !== state) {
    this.state  = state;
    this.emit('state', state);
  }
};

Transaction.prototype.serialize = function () {
  return SerializedObject.from_json(this.tx_json);
};

Transaction.prototype.signing_hash = function () {
  var prefix = config.testnet
        ? Transaction.HASH_SIGN_TESTNET
        : Transaction.HASH_SIGN;

  return SerializedObject.from_json(this.tx_json).signing_hash(prefix);
};

Transaction.prototype.sign = function () {
  var seed = Seed.from_json(this._secret),
      priv = seed.generate_private(this.tx_json.Account),
      hash = this.signing_hash();

  var key = new sjcl.ecc.ecdsa.secretKey(sjcl.ecc.curves['c256'], priv.to_bn()),
      sig = key.signDER(hash.to_bits(), 0),
      hex = sjcl.codec.hex.fromBits(sig).toUpperCase();

  this.tx_json.TxnSignature = hex;
};

// Submit a transaction to the network.
// XXX Don't allow a submit without knowing ledger_index.
// XXX Have a network canSubmit(), post events for following.
// XXX Also give broader status for tracking through network disconnects.
// callback = function (status, info) {
//   // status is final status.  Only works under a ledger_accepting conditions.
//   switch status:
//    case 'tesSUCCESS': all is well.
//    case 'tejServerUntrusted': sending secret to untrusted server.
//    case 'tejInvalidAccount': locally detected error.
//    case 'tejLost': locally gave up looking
//    default: some other TER
// }
Transaction.prototype.submit = function (callback) {
  var self    = this;
  var tx_json = this.tx_json;

  this.callback = callback;

  if ('string' !== typeof tx_json.Account)
  {
    (this.callback || this.emit)('error', {
        'error' : 'tejInvalidAccount',
        'error_message' : 'Bad account.'
      });
    return;
  }

  // YYY Might check paths for invalid accounts.

  if (this.remote.local_fee && undefined === tx_json.Fee) {
    tx_json.Fee    = Transaction.fees['default'].to_json();
  }

  if (this.callback || this.listeners('final').length || this.listeners('lost').length || this.listeners('pending').length) {
    // There are listeners for callback, 'final', 'lost', or 'pending' arrange to emit them.

    this.submit_index = this.remote._ledger_current_index;

    // When a ledger closes, look for the result.
    var on_ledger_closed = function (message) {
        var ledger_hash   = message.ledger_hash;
        var ledger_index  = message.ledger_index;
        var stop          = false;

// XXX make sure self.hash is available.
        self.remote.request_transaction_entry(self.hash)
          .ledger_hash(ledger_hash)
          .on('success', function (message) {
              self.set_state(message.metadata.TransactionResult);
              self.emit('final', message);

              if (self.callback)
                self.callback(message.metadata.TransactionResult, message);

              stop  = true;
            })
          .on('error', function (message) {
              if ('remoteError' === message.error
                && 'transactionNotFound' === message.remote.error) {
                if (self.submit_index + SUBMIT_LOST < ledger_index) {
                  self.set_state('client_lost');        // Gave up.
                  self.emit('lost');

                  if (self.callback)
                    self.callback('tejLost', message);

                  stop  = true;
                }
                else if (self.submit_index + SUBMIT_MISSING < ledger_index) {
                  self.set_state('client_missing');    // We don't know what happened to transaction, still might find.
                  self.emit('pending');
                }
                else {
                  self.emit('pending');
                }
              }
              // XXX Could log other unexpectedness.
            })
          .request();

        if (stop) {
          self.remote.removeListener('ledger_closed', on_ledger_closed);
          self.emit('final', message);
        }
      };

    this.remote.on('ledger_closed', on_ledger_closed);

    if (this.callback) {
      this.on('error', function (message) {
          self.callback(message.error, message);
        });
    }
  }

  this.set_state('client_submitted');

  this.remote.submit(this);

  return this;
}

//
// Set options for Transactions
//

// --> build: true, to have server blindly construct a path.
//
// "blindly" because the sender has no idea of the actual cost except that is must be less than send max.
Transaction.prototype.build_path = function (build) {
  this._build_path = build;

  return this;
}

// tag should be undefined or a 32 bit integer.   
// YYY Add range checking for tag.
Transaction.prototype.destination_tag = function (tag) {
  if (undefined !== tag)
   this.tx_json.DestinationTag = tag;

  return this;
}

Transaction._path_rewrite = function (path) {
  var path_new  = [];

  for (var index in path) {
    var node      = path[index];
    var node_new  = {};

    if ('account' in node)
      node_new.account  = UInt160.json_rewrite(node.account);

    if ('issuer' in node)
      node_new.issuer   = UInt160.json_rewrite(node.issuer);

    if ('currency' in node)
      node_new.currency = Currency.json_rewrite(node.currency);

    path_new.push(node_new);
  }

  return path_new;
}

Transaction.prototype.path_add = function (path) {
  this.tx_json.Paths  = this.tx_json.Paths || []
  this.tx_json.Paths.push(Transaction._path_rewrite(path));

  return this;
}

// --> paths: undefined or array of path
// A path is an array of objects containing some combination of: account, currency, issuer
Transaction.prototype.paths = function (paths) {
  for (var index in paths) {
    this.path_add(paths[index]);
  }

  return this;
}

// If the secret is in the config object, it does not need to be provided.
Transaction.prototype.secret = function (secret) {
  this._secret = secret;
}

Transaction.prototype.send_max = function (send_max) {
  if (send_max)
      this.tx_json.SendMax = Amount.json_rewrite(send_max);

  return this;
}

// tag should be undefined or a 32 bit integer.   
// YYY Add range checking for tag.
Transaction.prototype.source_tag = function (tag) {
  if (undefined !== tag)
   this.tx_json.SourceTag = tag;

  return this;
}

// --> rate: In billionths.
Transaction.prototype.transfer_rate = function (rate) {
  this.tx_json.TransferRate = Number(rate);

  if (this.tx_json.TransferRate < 1e9)
    throw 'invalidTransferRate';

  return this;
}

// Add flags to a transaction.
// --> flags: undefined, _flag_, or [ _flags_ ]
Transaction.prototype.set_flags = function (flags) {
  if (flags) {
      var transaction_flags = Transaction.flags[this.tx_json.TransactionType];

      if (undefined == this.tx_json.Flags)      // We plan to not define this field on new Transaction.
        this.tx_json.Flags        = 0;

      var flag_set = 'object' === typeof flags ? flags : [ flags ];

      for (var index in flag_set) {
        if (!flag_set.hasOwnProperty(index)) continue;

        var flag = flag_set[index];

        if (flag in transaction_flags) {
          this.tx_json.Flags += transaction_flags[flag];
        } else {
          // XXX Immediately report an error or mark it.
        }
      }
  }

  return this;
}

//
// Transactions
//

Transaction.prototype._account_secret = function (account) {
  // Fill in secret from remote, if available.
  return this.remote.secrets[account];
};

// Options:
//  .domain()           NYI
//  .message_key()      NYI
//  .transfer_rate()
//  .wallet_locator()   NYI
//  .wallet_size()      NYI
Transaction.prototype.account_set = function (src) {
  this._secret                  = this._account_secret(src);
  this.tx_json.TransactionType  = 'AccountSet';
  this.tx_json.Account          = UInt160.json_rewrite(src);

  return this;
};

Transaction.prototype.claim = function (src, generator, public_key, signature) {
  this._secret                  = this._account_secret(src);
  this.tx_json.TransactionType  = 'Claim';
  this.tx_json.Generator        = generator;
  this.tx_json.PublicKey        = public_key;
  this.tx_json.Signature        = signature;

  return this;
};

Transaction.prototype.offer_cancel = function (src, sequence) {
  this._secret                  = this._account_secret(src);
  this.tx_json.TransactionType  = 'OfferCancel';
  this.tx_json.Account          = UInt160.json_rewrite(src);
  this.tx_json.OfferSequence    = Number(sequence);

  return this;
};

// --> expiration : Date or Number
Transaction.prototype.offer_create = function (src, taker_pays, taker_gets, expiration) {
  this._secret                  = this._account_secret(src);
  this.tx_json.TransactionType  = 'OfferCreate';
  this.tx_json.Account          = UInt160.json_rewrite(src);
  this.tx_json.TakerPays        = Amount.json_rewrite(taker_pays);
  this.tx_json.TakerGets        = Amount.json_rewrite(taker_gets);

  if (this.remote.local_fee) {
    this.tx_json.Fee            = Transaction.fees.offer.to_json();
  }

  if (expiration)
    this.tx_json.Expiration  = Date === expiration.constructor
                                    ? expiration.getTime()
                                    : Number(expiration);

  return this;
};

Transaction.prototype.password_fund = function (src, dst) {
  this._secret                  = this._account_secret(src);
  this.tx_json.TransactionType  = 'PasswordFund';
  this.tx_json.Destination      = UInt160.json_rewrite(dst);

  return this;
}

Transaction.prototype.password_set = function (src, authorized_key, generator, public_key, signature) {
  this._secret                  = this._account_secret(src);
  this.tx_json.TransactionType  = 'PasswordSet';
  this.tx_json.RegularKey       = authorized_key;
  this.tx_json.Generator        = generator;
  this.tx_json.PublicKey        = public_key;
  this.tx_json.Signature        = signature;

  return this;
}

// Construct a 'payment' transaction.
//
// When a transaction is submitted:
// - If the connection is reliable and the server is not merely forwarding and is not malicious,
// --> src : UInt160 or String
// --> dst : UInt160 or String
// --> deliver_amount : Amount or String.
//
// Options:
//  .paths()
//  .build_path()
//  .destination_tag()
//  .path_add()
//  .secret()
//  .send_max()
//  .set_flags()
//  .source_tag()
Transaction.prototype.payment = function (src, dst, deliver_amount) {
  this._secret                  = this._account_secret(src);
  this.tx_json.TransactionType  = 'Payment';
  this.tx_json.Account          = UInt160.json_rewrite(src);
  this.tx_json.Amount           = Amount.json_rewrite(deliver_amount);
  this.tx_json.Destination      = UInt160.json_rewrite(dst);

  return this;
}

Transaction.prototype.ripple_line_set = function (src, limit, quality_in, quality_out) {
  this._secret                  = this._account_secret(src);
  this.tx_json.TransactionType  = 'TrustSet';
  this.tx_json.Account          = UInt160.json_rewrite(src);

  // Allow limit of 0 through.
  if (undefined !== limit)
    this.tx_json.LimitAmount  = Amount.json_rewrite(limit);

  if (quality_in)
    this.tx_json.QualityIn    = quality_in;

  if (quality_out)
    this.tx_json.QualityOut   = quality_out;

  // XXX Throw an error if nothing is set.

  return this;
};

Transaction.prototype.wallet_add = function (src, amount, authorized_key, public_key, signature) {
  this._secret                  = this._account_secret(src);
  this.tx_json.TransactionType  = 'WalletAdd';
  this.tx_json.Amount           = Amount.json_rewrite(amount);
  this.tx_json.RegularKey       = authorized_key;
  this.tx_json.PublicKey        = public_key;
  this.tx_json.Signature        = signature;

  return this;
};

exports.Transaction     = Transaction;

// vim:sw=2:sts=2:ts=8:et
