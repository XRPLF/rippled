var async       = require("async");
var assert      = require('assert');
var Amount      = require("ripple-lib").Amount;
var Remote      = require("ripple-lib").Remote;
var Transaction = require("ripple-lib").Transaction;
var Server      = require("./server").Server;
var testutils   = require("./testutils");
var config      = testutils.init_config();

function makeOffer($, account, takerpays, takergets, callback) {
  $.remote.transaction()
    .offer_create(account, takerpays, takergets)
    .on('submitted', function (m) {
        // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
        // console.log("PROPOSED: offer_create: Result %s. TakerGets %s. TakerPays %s.", m.engine_result, JSON.stringify(m.tx_json.TakerGets), JSON.stringify(m.tx_json.TakerPays));
        assert.strictEqual('tesSUCCESS', m.engine_result);
        $.remote
          .once('ledger_closed', function (message) {
              // console.log("LEDGER_CLOSED: %d: %s", message.ledger_index, message.ledger_hash);
              assert(message);
            })
          .ledger_accept();
      })
    .on('final', function (m) {
        // console.log("FINAL: offer_create: %s", JSON.stringify(m));
        // console.log("FINAL: offer_create: Result %s. Validated %s. TakerGets %s. TakerPays %s.", m.engine_result, m.validated, JSON.stringify(m.tx_json.TakerGets), JSON.stringify(m.tx_json.TakerPays));

        callback(m.metadata.TransactionResult !== 'tesSUCCESS');
    })
    .submit();
}

function makeOfferWithEvent($, account, takerpays, takergets, callback) {
  $.remote.once('transaction', function(m) {
    // console.log("TRANSACTION: %s", JSON.stringify(m));
    // console.log("TRANSACTION: meta: %s, takerpays: %s, takergets: %s", m.meta.AffectedNodes.length, Amount.from_json(m.transaction.TakerPays).to_human_full(), Amount.from_json(m.transaction.TakerGets).to_human_full());

    assert.strictEqual(Amount.from_json(takergets).to_human_full(),
      Amount.from_json(m.transaction.TakerGets).to_human_full());
    assert.strictEqual(Amount.from_json(takerpays).to_human_full(),
      Amount.from_json(m.transaction.TakerPays).to_human_full());
    assert.strictEqual("OfferCreate", m.transaction.TransactionType);
    // We don't get quality from the event
    var foundOffer = false;
    var foundRoot = false;
    for(var n = m.meta.AffectedNodes.length-1; n >= 0; --n) {
      var node = m.meta.AffectedNodes[n];
      // console.log(node);
      if(node.CreatedNode && 
        node.CreatedNode.LedgerEntryType === "Offer") {
          foundOffer = true;
          var fields = node.CreatedNode.NewFields;
          assert.strictEqual(Amount.from_json(takergets).to_human_full(),
            Amount.from_json(fields.TakerGets).to_human_full());
          assert.strictEqual(Amount.from_json(takerpays).to_human_full(),
            Amount.from_json(fields.TakerPays).to_human_full());
      } else if (node.ModifiedNode) {
        assert(node.ModifiedNode.LedgerEntryType != "Offer");
        if(node.ModifiedNode.LedgerEntryType === "AccountRoot") {
          foundRoot = true;
          var mod = node.ModifiedNode;
          assert(mod.FinalFields.OwnerCount == 
              mod.PreviousFields.OwnerCount + 1);
        }
      }
    }
    assert(foundOffer);
    assert(foundRoot);

    callback(m.engine_result !== 'tesSUCCESS');
  });
  makeOffer($, account, takerpays, takergets, function () {});
}

function matchOffers(expected, actual) {
  assert.strictEqual(expected.length, actual.length);
  var found = [];
  for(var i = 0; i < actual.length; ++i) {
    var offer = actual[i];
    // console.log("Got: ", offer);
    for(var j = 0; j < expected.length; ++j) {
      var expectedOffer = expected[j];
      // console.log("checking: ", expectedOffer);
      if(Amount.from_json(expectedOffer.takerGets).to_human_full() ===
        Amount.from_json(offer.TakerGets).to_human_full()
        && Amount.from_json(expectedOffer.takerPays).to_human_full() ===
        Amount.from_json(offer.TakerPays).to_human_full()
        && expectedOffer.quality === offer.quality) {
        // console.log("FOUND");
        found.push(expectedOffer);
        expected.splice(j, 1);
        break;
      }
    }
  }
  if(expected.length != 0 || actual.length != found.length) {
    console.log("Received: ", actual.length);
    for(i = 0; i < actual.length; ++i) {
      var offer = actual[i];
      console.log("  TakerGets: %s, TakerPays %s",
          Amount.from_json(offer.TakerGets).to_human_full(),
          Amount.from_json(offer.TakerPays).to_human_full());
    }
    console.log("Found: ", found.length);
    for(i = 0; i < found.length; ++i) {
      var offer = found[i];
      console.log("  TakerGets: %s, TakerPays %s", offer.takerGets,
          offer.takerPays);
    }
    console.log("Not found: ", expected.length);
    for(i = 0; i < expected.length; ++i) {
      var offer = expected[i];
      console.log("  TakerGets: %s, TakerPays %s", offer.takerGets,
          offer.takerPays);
    }
  }
  assert.strictEqual(0, expected.length);
  assert.strictEqual(actual.length, found.length);
}

function buildOfferFunctions($, offers) {
  var functions = [];
  for(var i = 0; i < offers.length; ++i) {
    var offer = offers[i];
    functions.push(function(offer) {
      // console.log("Offer pre: ", offer);
      return function(callback) {
        // console.log("Offer in: ", offer);
        makeOffer($, "root", offer.takerPays, offer.takerGets, callback);
      }
    }(offer));
  }
  return functions;
}

suite("Subscribe book tests", function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test("One side: Empty book", function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            var request = $.remote.requestSubscribe(null);
            request.addBook({
              "taker_gets" : {
                  "currency" : "XRP"
              },
              "taker_pays" : {
                  "currency" : "USD", "issuer" : "root"
              }
            }, true);
            request.once('success', function(res) {
              // console.log("SUBSCRIBE: %s", JSON.stringify(res));

              assert.strictEqual(0, res.offers.length);
              assert(!res.asks);
              assert(!res.bids);

              callback(null);
            });
            request.once('error', function(err) {
              // console.log(err);
              callback(err);
            });
            request.request();
          },
          function (callback) {
            // Make another ask. Make sure we get notified
            makeOfferWithEvent($, "root", "700", "100/USD/root", callback);
          },
          function (callback) {
            // Make another bid. Make sure we get notified
            makeOfferWithEvent($, "root", "100/USD/root", "75", callback);
          }
        ], function (error) {
          // console.log("result: error=%s", error);
          assert(!error, self.what || "Unspecified Error");

          done();
        });
  });

  test("One side: Offers in book", function (done) {
      var self = this;
      var askTakerPays = "500";
      var askTakerGets = "100/usd/root";
      var askQuality = "5";
      var bidTakerPays = askTakerGets;
      var bidTakerGets = "200";
      var bidQuality = "0.5";

      async.waterfall([
          function(callback) {
            // Create an ask: TakerPays 500, TakerGets 100/USD
            makeOffer($, "root", askTakerPays, askTakerGets, callback);
          },
          function(callback) {
            // Create an bid: TakerPays 100/USD, TakerGets 1200
            makeOffer($, "root", bidTakerPays, bidTakerGets, callback);
          },
          function (callback) {
            var request = $.remote.requestSubscribe(null);
            request.addBook({
              "taker_gets" : {
                  "currency" : "XRP"
              },
              "taker_pays" : {
                  "currency" : "USD", "issuer" : "root"
              }
            }, true);
            request.once('success', function(res) {
              // console.log("SUBSCRIBE: %s", JSON.stringify(res));

              assert.strictEqual(1, res.offers.length);
              var bid = res.offers[0];
              assert.strictEqual(Amount.from_json(bidTakerGets).to_human_full(),
                Amount.from_json(bid.TakerGets).to_human_full());
              assert.strictEqual(Amount.from_json(bidTakerPays).to_human_full(),
                Amount.from_json(bid.TakerPays).to_human_full());
              assert.strictEqual(bidQuality, bid.quality);

              assert(!res.asks);
              assert(!res.bids);

              callback(null);
            });
            request.once('error', function(err) {
              // console.log(err);
              callback(err);
            });
            request.request();
          },
          function (callback) {
            // Make another ask. Make sure we get notified
            makeOfferWithEvent($, "root", "700", "100/USD/root", callback);
          },
          function (callback) {
            // Make another bid. Make sure we get notified
            makeOfferWithEvent($, "root", "100/USD/root", "75", callback);
          }
        ], function (error) {
          // console.log("result: error=%s", error);
          assert(!error, self.what || "Unspecified Error");

          done();
        });
  });

  test("Both sides: Empty book", function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            var request = $.remote.requestSubscribe(null);
            request.addBook({
              "both" : true,
              "taker_gets" : {
                  "currency" : "XRP"
              },
              "taker_pays" : {
                  "currency" : "USD", "issuer" : "root"
              }
            }, true);
            request.once('success', function(res) {
              // console.log("SUBSCRIBE: %s", JSON.stringify(res));

              assert.strictEqual(0, res.asks.length);
              assert.strictEqual(0, res.bids.length);
              assert(!res.offers);

              callback(null);
            });
            request.once('error', function(err) {
              // console.log(err);
              callback(err);
            });
            request.request();
          },
          function (callback) {
            // Make another ask. Make sure we get notified
            makeOfferWithEvent($, "root", "700", "100/USD/root", callback);
          },
          function (callback) {
            // Make another bid. Make sure we get notified
            makeOfferWithEvent($, "root", "100/USD/root", "75", callback);
          }
        ], function (error) {
          // console.log("result: error=%s", error);
          assert(!error, self.what || "Unspecified Error");

          done();
        });
  });

  test("Both sides: Offers in book", function (done) {
      var self = this;
      var askTakerPays = "500";
      var askTakerGets = "100/USD/root";
      var askQuality = "5";
      var bidTakerPays = askTakerGets;
      var bidTakerGets = "200";
      var bidQuality = "0.5";

      async.waterfall([
          function(callback) {
            // Create an ask: TakerPays 500, TakerGets 100/USD
            makeOffer($, "root", askTakerPays, askTakerGets, callback);
          },
          function(callback) {
            // Create an bid: TakerPays 100/USD, TakerGets 1200
            makeOffer($, "root", bidTakerPays, bidTakerGets, callback);
          },
          function (callback) {
            var request = $.remote.requestSubscribe(null);
            request.addBook({
              "both" : true,
              "taker_gets" : {
                  "currency" : "XRP"
              },
              "taker_pays" : {
                  "currency" : "USD", "issuer" : "root"
              }
            }, true);
            request.once('success', function(res) {
              // console.log("SUBSCRIBE: %s", JSON.stringify(res));

              assert.strictEqual(1, res.asks.length);
              var ask = res.asks[0];
              assert.strictEqual(Amount.from_json(askTakerGets).to_human_full(),
                Amount.from_json(ask.TakerGets).to_human_full());
              assert.strictEqual(Amount.from_json(askTakerPays).to_human_full(),
                Amount.from_json(ask.TakerPays).to_human_full());
              assert.strictEqual(askQuality, ask.quality);

              assert.strictEqual(1, res.bids.length);
              var bid = res.bids[0];
              assert.strictEqual(Amount.from_json(bidTakerGets).to_human_full(),
                Amount.from_json(bid.TakerGets).to_human_full());
              assert.strictEqual(Amount.from_json(bidTakerPays).to_human_full(),
                Amount.from_json(bid.TakerPays).to_human_full());
              assert.strictEqual(bidQuality, bid.quality);

              assert(!res.offers);

              callback(null);
            });
            request.once('error', function(err) {
              // console.log(err);
              callback(err);
            });
            request.request();
          },
          function (callback) {
            // Make another ask. Make sure we get notified
            makeOfferWithEvent($, "root", "700", "100/USD/root", callback);
          },
          function (callback) {
            // Make another bid. Make sure we get notified
            makeOfferWithEvent($, "root", "100/USD/root", "75", callback);
          }
        ], function (error) {
          // console.log("result: error=%s", error);
          assert(!error, self.what || "Unspecified Error");

          done();
        });
  });

  test("Multiple books: One side: Empty book", function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            var request = $.remote.requestSubscribe(null);
            request.addBook({
              "taker_gets" : {
                  "currency" : "XRP"
              },
              "taker_pays" : {
                  "currency" : "USD", "issuer" : "root"
              }
            }, true);
            request.addBook({
              "taker_gets" : {
                  "currency" : "CNY", "issuer" : "root"
              },
              "taker_pays" : {
                  "currency" : "JPY", "issuer" : "root"
              }
            }, true);
            request.once('success', function(res) {
              // console.log("SUBSCRIBE: %s", JSON.stringify(res));

              assert.strictEqual(0, res.offers.length);
              assert(!res.asks);
              assert(!res.bids);

              callback(null);
            });
            request.once('error', function(err) {
              // console.log(err);
              callback(err);
            });
            request.request();
          },
          function (callback) {
            // Make another ask. Make sure we get notified
            makeOfferWithEvent($, "root", "700", "100/USD/root", callback);
          },
          function (callback) {
            // Make another bid. Make sure we get notified
            makeOfferWithEvent($, "root", "100/USD/root", "75", callback);
          },
          function (callback) {
            // Make another ask. Make sure we get notified
            makeOfferWithEvent($, "root", "700/CNY/root", "100/JPY/root", callback);
          },
          function (callback) {
            // Make another bid. Make sure we get notified
            makeOfferWithEvent($, "root", "100/JPY/root", "75/CNY/root", callback);
          }
        ], function (error) {
          // console.log("result: error=%s", error);
          assert(!error, self.what || "Unspecified Error");

          done();
        });
  });

  test("Multiple books: One side: Offers in book", function (done) {
      var self = this;
      var asks = [
        { takerPays: "500", takerGets: "100/usd/root", quality: "5" },
        { takerPays: "500/cny/root", takerGets: "100/jpy/root", quality: "5" },
      ];
      var bids = [
        { takerPays: "100/usd/root", takerGets: "200", quality: "0.5" },
        { takerPays: "100/jpy/root", takerGets: "200/cny/root", quality: "0.5" },
      ];

      var functions = buildOfferFunctions($, asks)
        .concat(buildOfferFunctions($, bids));

      async.waterfall(
        functions.concat([
          function (callback) {
            var request = $.remote.requestSubscribe(null);
            request.addBook({
              "taker_gets" : {
                  "currency" : "XRP"
              },
              "taker_pays" : {
                  "currency" : "USD", "issuer" : "root"
              }
            }, true);
            request.addBook({
              "taker_gets" : {
                  "currency" : "CNY", "issuer" : "root"
              },
              "taker_pays" : {
                  "currency" : "JPY", "issuer" : "root"
              }
            }, true);
            request.once('success', function(res) {
              // console.log("SUBSCRIBE: %s", JSON.stringify(res));

              matchOffers(bids, res.offers);

              assert(!res.asks);
              assert(!res.bids);

              callback(null);
            });
            request.once('error', function(err) {
              // console.log(err);
              callback(err);
            });
            request.request();
          },
          function (callback) {
            // Make another ask. Make sure we get notified
            makeOfferWithEvent($, "root", "700", "100/USD/root", callback);
          },
          function (callback) {
            // Make another bid. Make sure we get notified
            makeOfferWithEvent($, "root", "100/USD/root", "75", callback);
          },
          function (callback) {
            // Make another ask. Make sure we get notified
            makeOfferWithEvent($, "root", "700/CNY/root", "100/JPY/root", callback);
          },
          function (callback) {
            // Make another bid. Make sure we get notified
            makeOfferWithEvent($, "root", "100/JPY/root", "75/CNY/root", callback);
          }
        ]), function (error) {
          // console.log("result: error=%s", error);
          assert(!error, self.what || "Unspecified Error");

          done();
        });
  });

  test("Multiple books: Both sides: Empty book", function (done) {
      var self = this;

      async.waterfall([
          function (callback) {
            var request = $.remote.requestSubscribe(null);
            request.addBook({
              "both" : true,
              "taker_gets" : {
                  "currency" : "XRP"
              },
              "taker_pays" : {
                  "currency" : "USD", "issuer" : "root"
              }
            }, true);
            request.addBook({
              "both" : true,
              "taker_gets" : {
                  "currency" : "CNY", "issuer" : "root"
              },
              "taker_pays" : {
                  "currency" : "JPY", "issuer" : "root"
              }
            }, true);
            request.once('success', function(res) {
              // console.log("SUBSCRIBE: %s", JSON.stringify(res));

              assert.strictEqual(0, res.asks.length);
              assert.strictEqual(0, res.bids.length);
              assert(!res.offers);

              callback(null);
            });
            request.once('error', function(err) {
              // console.log(err);
              callback(err);
            });
            request.request();
          },
          function (callback) {
            // Make another ask. Make sure we get notified
            makeOfferWithEvent($, "root", "700", "100/USD/root", callback);
          },
          function (callback) {
            // Make another bid. Make sure we get notified
            makeOfferWithEvent($, "root", "100/USD/root", "75", callback);
          },
          function (callback) {
            // Make another ask. Make sure we get notified
            makeOfferWithEvent($, "root", "700/CNY/root", "100/JPY/root", callback);
          },
          function (callback) {
            // Make another bid. Make sure we get notified
            makeOfferWithEvent($, "root", "100/JPY/root", "75/CNY/root", callback);
          }
        ], function (error) {
          // console.log("result: error=%s", error);
          assert(!error, self.what || "Unspecified Error");

          done();
        });
  });

  test("Multiple books: Both sides: Offers in book", function (done) {
      var self = this;
      var asks = [
        { takerPays: "500", takerGets: "100/usd/root", quality: "5" },
        { takerPays: "500/cny/root", takerGets: "100/jpy/root", quality: "5" },
      ];
      var bids = [
        { takerPays: "100/usd/root", takerGets: "200", quality: "0.5" },
        { takerPays: "100/jpy/root", takerGets: "200/cny/root", quality: "0.5" },
      ];

      var functions = buildOfferFunctions($, asks)
        .concat(buildOfferFunctions($, bids));

      async.waterfall(
        functions.concat([
          function (callback) {
            var request = $.remote.requestSubscribe(null);
            request.addBook({
              "both" : true,
              "taker_gets" : {
                  "currency" : "XRP"
              },
              "taker_pays" : {
                  "currency" : "USD", "issuer" : "root"
              }
            }, true);
            request.addBook({
              "both" : true,
              "taker_gets" : {
                  "currency" : "CNY", "issuer" : "root"
              },
              "taker_pays" : {
                  "currency" : "JPY", "issuer" : "root"
              }
            }, true);
            request.once('success', function(res) {
              // console.log("SUBSCRIBE: %s", JSON.stringify(res));

              matchOffers(asks, res.asks);
              matchOffers(bids, res.bids);

              assert(!res.offers);

              callback(null);
            });
            request.once('error', function(err) {
              // console.log(err);
              callback(err);
            });
            request.request();
          },
          function (callback) {
            // Make another ask. Make sure we get notified
            makeOfferWithEvent($, "root", "700", "100/USD/root", callback);
          },
          function (callback) {
            // Make another bid. Make sure we get notified
            makeOfferWithEvent($, "root", "100/USD/root", "75", callback);
          },
          function (callback) {
            // Make another ask. Make sure we get notified
            makeOfferWithEvent($, "root", "700/CNY/root", "100/JPY/root", callback);
          },
          function (callback) {
            // Make another bid. Make sure we get notified
            makeOfferWithEvent($, "root", "100/JPY/root", "75/CNY/root", callback);
          }
        ]), function (error) {
          // console.log("result: error=%s", error);
          assert(!error, self.what || "Unspecified Error");

          done();
        });
  });

});
