////var assert      = require('assert');
////var async       = require("async");
////var extend      = require('extend');
////var Amount      = require("ripple-lib").Amount;
////var Remote      = require("ripple-lib").Remote;
////var Transaction = require("ripple-lib").Transaction;
////var RippleError = require("ripple-lib").RippleError;
////var Server      = require("./server").Server;
////var testutils   = require("./testutils");
////var config      = testutils.init_config();
////
////// Hard-coded limits we'll be testing:
////var BINARY_LIMIT    = 500;
////var NONBINARY_LIMIT = 200;
////
////var ACCOUNT         = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
////var OFFSET          = 180;
////var LIMIT           = 170;
////
////var FIRST_BATCH     = 199; // Within both limits
////var SECOND_BATCH    = 10; // Between NONBINARY_LIMIT and BINARY_LIMIT
////var THIRD_BATCH     = 295; // Exceeds both limits
////var VERBOSE         = false;
////
////
////suite('Account_tx tests', function() {
////  var $ = { };
////
////  setup(function(done) {
////    testutils.build_setup().call($, done);
////  });
////
////  teardown(function(done) {
////    testutils.build_teardown().call($, done);
////  });
////
////  test('make many transactions and query using account_tx', function(done) {
////    var root_id = $.remote.account('root')._account_id;
////    $.remote.request_subscribe().accounts(root_id).request();
////
////    var self = this;
////    var transactionCounter = 0;
////    var final_create;
////
////    function createOffer(callback) {
////      var tx = $.remote.transaction();
////      tx.offer_create("root", "500", "100/USD/root");
////
////      tx.once('proposed', function(m) {
////        $.remote.ledger_accept();
////      });
////
////      tx.once('error', callback);
////
////      tx.once('final', function(m) {
////        callback();
////      });
////
////      tx.submit();
////    };
////
////    function lotsOfTransactions(count, callback) {
////      ;(function nextOffer(i) {
////        createOffer(function(err) {
////          console.log(i, count);
////          if (err) callback(err);
////          else if (++i === count) callback(null);
////          else nextOffer(i);
////        });
////      })(0);
////    };
////
////    function firstBatch() {
////      lotsOfTransactions(FIRST_BATCH, function(a, b) {
////        runTests(self, FIRST_BATCH, 0, void(0), function() {
////          console.log('2');
////          runTests(self, FIRST_BATCH, OFFSET, void(0), function() {
////            console.log('3');
////            runTests(self, FIRST_BATCH, -1, LIMIT, secondBatch);
////          })
////        })});
////    }
////
////    function secondBatch() {
////      lotsOfTransactions(SECOND_BATCH, function() {
////        runTests(self, FIRST_BATCH+SECOND_BATCH, 0, void(0), function() {
////          runTests(self, FIRST_BATCH+SECOND_BATCH, OFFSET, void(0), thirdBatch);
////        })});
////    }
////
////    function thirdBatch() {
////      lotsOfTransactions(THIRD_BATCH, function() {
////        runTests(self, FIRST_BATCH+SECOND_BATCH+THIRD_BATCH, 0, void(0), function() {
////          runTests(self, FIRST_BATCH+SECOND_BATCH+THIRD_BATCH, OFFSET, void(0), done);
////        })});
////    }
////
////    firstBatch();
////
////    function errorHandler(callback) {
////      return function(r) {
////        if (VERBOSE) console.log("ERROR!");
////        if (VERBOSE) console.log(r);
////        callback(r);
////      }
////    }
////
////    function txOptions(ext) {
////      var defaults = {
////        account:           ACCOUNT,
////        ledger_index_min:  -1,
////        ledger_index_max:  -1
////      }
////      return extend(defaults, ext);
////    };
////
////    function sortByLedger(a, b) {
////      assert(a.tx, 'Transaction missing');
////      assert(b.tx, 'Transaction missing');
////      if (a.tx.inLedger > b.tx.inLedger) {
////        return 1;
////      } else if (a.tx.inLedger < b.tx.inLedger) {
////        return -1;
////      } else {
////        return 0;
////      }
////    };
////
////    function runTests(self, transactionCount, offset, limit, callback) {
////      var steps = [
////        function(callback) {
////          if (VERBOSE) console.log('nonbinary');
////
////          var req = $.remote.request_account_tx(txOptions({ offset: offset, limit: limit }));
////
////          req.callback(function(err, r) {
////            if (err) return callback(err);
////            assert(r && r.transactions);
////            var targetLength = Math.min(NONBINARY_LIMIT, limit ? Math.min(limit, transactionCount - offset) : transactionCount - offset);
////            assert.strictEqual(r.transactions.length, targetLength, 'Transactions unexpected length');
////            //assert.deepEqual(r.transactions.sort(sortByLedger), r.transactions, 'Transactions out of order');
////            callback();
////          });
////        },
////
////        function(callback) {
////          if (VERBOSE) console.log('binary');
////
////          var req = $.remote.request_account_tx(txOptions({ binary: true, offset: offset, limit: limit }));
////
////          req.callback(function(err, r) {
////            if (err) return callback(err);
////            assert(r && r.transactions);
////            var targetLength = Math.min(BINARY_LIMIT, limit ? Math.min(limit, transactionCount-offset) : transactionCount-offset);
////            assert.strictEqual(r.transactions.length, targetLength, 'Transactions unexpected length');
////            //assert.deepEqual(r.transactions.sort(sortByLedger), r.transactions, 'Transactions out of order');
////            callback();
////          });
////        },
////
////        function(callback) {
////          if (VERBOSE) console.log('nonbinary+offset');
////
////          var req = $.remote.request_account_tx(txOptions({ descending: true, offset: offset, limit: limit }));
////
////          req.callback(function(err, r) {
////            if (err) return callback(err);
////            assert(r && r.transactions);
////            var targetLength = Math.min(NONBINARY_LIMIT, limit ? Math.min(limit,transactionCount-offset) : transactionCount-offset );
////            assert.strictEqual(r.transactions.length, targetLength, 'Transactions unexpected length');
////            //assert.deepEqual(r.transactions.sort(sortByLedger), r.transactions, 'Transactions out of order');
////            callback();
////          });
////        }
////      ]
////
////      async.series(steps, function(error) {
////        console.log(error);
////        assert.ifError(error);
////        callback();
////      });
////    }
////  });
////});
//=======
//var async       = require("async");
//var buster      = require("buster");
//
//var Amount      = require("ripple-lib").Amount;
//var Remote      = require("ripple-lib").Remote;
//var Transaction = require("ripple-lib").Transaction;
//var Server      = require("./server").Server;
//
//var testutils   = require("./testutils");
//var config      = testutils.init_config();
//
//buster.testRunner.timeout = 350000; //This is a very long test!
//
//
//// Hard-coded limits we'll be testing:
//var BINARY_LIMIT = 500;
//var NONBINARY_LIMIT = 200;
//
//var ACCOUNT = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
//var FIRST_BATCH = 199; // Within both limits
//var OFFSET = 180;
//var LIMIT = 170;
//var SECOND_BATCH = 10; // Between NONBINARY_LIMIT and BINARY_LIMIT
//var THIRD_BATCH = 295; // Exceeds both limits
//var VERBOSE = false;
//
//buster.testCase("//Account_tx tests", {
//  'setUp'     : testutils.build_setup(),
//  'tearDown'  : testutils.build_teardown(),
//
//  "make a lot of transactions and query using account_tx" : function (done) {
//    var self = this;
//    var final_create;
//    var transactionCounter = 0;
//    var f = 0;
//    var functionHolder;
//    var createOfferFunction = function (callback) {
//      self.remote.transaction()
//      .offer_create("root", "500", "100/USD/root")
//      .on('proposed', function (m) {
//        transactionCounter++;
//        if (VERBOSE) console.log('Submitted transaction', transactionCounter);
//
//        callback(m.result !== 'tesSUCCESS');
//      })
//      .on('final', function (m) {
//        f++;
//        if (VERBOSE) console.log("Finalized transaction", f);
//        buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);
//        buster.assert(final_create);
//        if ( f == transactionCounter ) {
//          if (VERBOSE) console.log("All transactions have been finalized.");
//          functionHolder();
//        }
//      })
//      .submit();
//    };
//
//    function lotsOfTransactions(number, whenDone) {
//      var bunchOfOffers = [];
//      for (var i=0; i<number; i++) {
//        bunchOfOffers.push(createOfferFunction);
//      }
//      functionHolder = whenDone; //lolwut
//      async.parallel(bunchOfOffers, function (error) {
//        if (VERBOSE) console.log("Accepting ledger.");
//        buster.refute(error);
//        self.remote
//        .once('ledger_closed', function (message) {
//          final_create = message;
//        })
//        .ledger_accept();
//      });
//    }
//
//    function firstBatch() {
//      lotsOfTransactions(FIRST_BATCH,
//                         function(){runTests(self, FIRST_BATCH, 0,      undefined, 
//                                             function(){runTests(self, FIRST_BATCH, OFFSET, undefined, 
//                                                                 function(){runTests(self, FIRST_BATCH, 0,      LIMIT,    secondBatch)})}
//                                            )});
//    }
//
//    function secondBatch() {
//      lotsOfTransactions(SECOND_BATCH,
//                         function(){runTests(self, FIRST_BATCH+SECOND_BATCH, 0,      undefined, 
//                                             function(){runTests(self, FIRST_BATCH+SECOND_BATCH, OFFSET, undefined, thirdBatch)}
//                                            )});
//    }
//
//    function thirdBatch() {
//      lotsOfTransactions(THIRD_BATCH,
//                         function(){runTests(self, FIRST_BATCH+SECOND_BATCH+THIRD_BATCH, 0,      undefined, 
//                                             function(){runTests(self, FIRST_BATCH+SECOND_BATCH+THIRD_BATCH, OFFSET, undefined, done)}
//                                            )});
//    }
//
//    firstBatch();
//
//
//    function standardErrorHandler(callback) {
//      return function(r) {
//        if (VERBOSE) console.log("ERROR!");
//        if (VERBOSE) console.log(r);
//        callback(r);
//      }
//    }
//
//
//    function runTests(self, actualNumberOfTransactions, offset, limit, finalCallback) {
//      if (VERBOSE) console.log("Testing batch with offset and limit:", offset, limit);
//      async.series([
//                   function(callback) {
//        if (VERBOSE) console.log('nonbinary');
//        self.remote.request_account_tx({
//          account:ACCOUNT,
//          ledger_index_min:-1,
//          ledger_index_max:-1,
//          offset:offset,
//          limit:limit
//        }).on('success', function (r) {
//          if (r.transactions) {
//            var targetLength = Math.min(NONBINARY_LIMIT, limit ? Math.min(limit,actualNumberOfTransactions-offset) : actualNumberOfTransactions-offset);
//            buster.assert(r.transactions.length == targetLength, "Got "+r.transactions.length+" transactions; expected "+targetLength );
//            //Check for proper ordering.
//            for (var i=0; i<r.transactions.length-1; i++) {
//              var t1 = r.transactions[i].tx;
//              var t2 = r.transactions[i+1].tx;
//              buster.assert(t1.inLedger<=t2.inLedger, 
//                            "Transactions were not ordered correctly: "+t1.inLedger+"#"+t1.Sequence+" should not have come before "+t2.inLedger+"#"+t2.Sequence);
//            }
//          } else {
//            buster.assert(r.transactions, "No transactions returned: "+offset+" "+limit);
//          }
//
//          callback(false);
//        })
//        .on('error', standardErrorHandler(callback))
//        .request();
//      },
//
//      function(callback) {
//        if (VERBOSE) console.log('binary');
//        self.remote.request_account_tx({
//          account:ACCOUNT,
//          ledger_index_min:-1,
//          ledger_index_max:-1,
//          binary:true,
//          offset:offset,
//          limit:limit
//        }).on('success', function (r) {
//          if (r.transactions) {
//            var targetLength = Math.min(BINARY_LIMIT, limit ? Math.min(limit,actualNumberOfTransactions-offset) : actualNumberOfTransactions-offset);
//            buster.assert(r.transactions.length == targetLength, "Got "+r.transactions.length+" transactions; expected "+targetLength );
//          } else {
//            buster.assert(r.transactions, "No transactions returned: "+offset+" "+limit);
//          }
//          callback(false);
//        })
//        .on('error', standardErrorHandler(callback))
//        .request();
//      },
//
//      function(callback) {
//        if (VERBOSE) console.log('nonbinary+offset');
//        self.remote.request_account_tx({
//          account:ACCOUNT,
//          ledger_index_min:-1,
//          ledger_index_max:-1,
//          descending:true,
//          offset:offset,
//          limit:limit
//        }).on('success', function (r) {
//          if (r.transactions) {	
//            var targetLength = Math.min(NONBINARY_LIMIT, limit ? Math.min(limit,actualNumberOfTransactions-offset) : actualNumberOfTransactions-offset );
//            buster.assert(r.transactions.length == targetLength, "Got "+r.transactions.length+" transactions; expected "+targetLength );
//            //Check for proper ordering.
//            for (var i=0; i<r.transactions.length-1; i++) {
//              var t1 = r.transactions[i].tx;
//              var t2 = r.transactions[i+1].tx;
//              //buster.assert(t1.inLedger>t2.inLedger  ||  (t1.inLedger==t2.inLedger && t1.Sequence > t2.Sequence  ),
//              //	"Transactions were not ordered correctly: "+t1.inLedger+"#"+t1.Sequence+" should not have come before "+t2.inLedger+"#"+t2.Sequence);
//              buster.assert(t1.inLedger>=t2.inLedger,
//                            "Transactions were not ordered correctly: "+t1.inLedger+"#"+t1.Sequence+" should not have come before "+t2.inLedger+"#"+t2.Sequence);
//            }
//          } else {
//            buster.assert(r.transactions, "No transactions returned: "+offset+" "+limit);
//          }
//
//
//          callback(false);
//        })
//        .on('error', standardErrorHandler(callback))
//        .request();
//      },
//
//
//      ], function (error) {
//        buster.refute(error);
//        finalCallback();
//      }
//                  );
//    }
//  }
//});
//
//
//
//// TODO:
//// Test the "count" feature.
