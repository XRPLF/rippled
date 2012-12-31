var async     = require("async");
var buster    = require("buster");
var http      = require("http");
var jsonrpc   = require("simple-jsonrpc");
var EventEmitter  = require('events').EventEmitter;

var Amount    = require("../src/js/amount.js").Amount;
var Remote    = require("../src/js/remote.js").Remote;
var Server    = require("./server.js").Server;

var testutils = require("./testutils.js");

var config = require("./config.js");

require("../src/js/amount.js").config = require("./config.js");
require("../src/js/remote.js").config = require("./config.js");

// How long to wait for server to start.
var serverDelay = 1500;

buster.testRunner.timeout = 5000;

var HttpServer = function () {
};

var server;
var server_events;

var build_setup = function (options) {
  var setup = testutils.build_setup(options);

  return function (done) {
      var self  = this;

      var http_config = config.http_servers["zed"];

      server_events = new EventEmitter;
      server = http.createServer(function (req, res) {
          // console.log("REQUEST");
          var input = "";

          req.setEncoding();

          req.on('data', function (buffer) {
              // console.log("DATA: %s", buffer);

              input = input + buffer;
            });

          req.on('end', function () {
              // console.log("END");
              var request = JSON.parse(input);

              // console.log("REQ: %s", JSON.stringify(request, undefined, 2));

              server_events.emit('request', request, res);
            });

          req.on('close', function () {
              // console.log("CLOSE");
            });
        });

      server.listen(http_config.port, http_config.ip, undefined,
        function () {
          // console.log("server up: %s %d", http_config.ip, http_config.port);

          setup.call(self, done);
        });
    };
};

var build_teardown = function () {
  var teardown = testutils.build_teardown();

  return function (done) {
    var self  = this;

    server.close(function () {
        // console.log("server closed");
    
        teardown.call(self, done);
      });
  };
};

buster.testCase("JSON-RPC", {
  setUp     : build_setup(),
  // setUp     : build_setup({ verbose: true }),
  // setUp     : build_setup({verbose: true , no_server: true}),
  tearDown  : build_teardown(),

  "server_info" :
    function (done) {
      var rippled_config = config.servers.alpha;
      var client  = jsonrpc.client("http://" + rippled_config.rpc_ip + ":" + rippled_config.rpc_port);

      client.call('server_info', [], function (result) {
          // console.log(JSON.stringify(result, undefined, 2));
          buster.assert('info' in result);

          done();
        });
    },

  "subscribe server" :
    function (done) {
      var rippled_config = config.servers.alpha;
      var client  = jsonrpc.client("http://" + rippled_config.rpc_ip + ":" + rippled_config.rpc_port);
      var http_config = config.http_servers["zed"];

      client.call('subscribe', [{
          'url' :  "http://" + http_config.ip + ":" + http_config.port,
          'streams' : [ 'server' ],
        }], function (result) {
          console.log(JSON.stringify(result, undefined, 2));
          buster.assert('random' in result);

          done();
        });
    },

  "=>subscribe ledger" :
    function (done) {
      var self = this;

      var rippled_config = config.servers.alpha;
      var client  = jsonrpc.client("http://" + rippled_config.rpc_ip + ":" + rippled_config.rpc_port);
      var http_config = config.http_servers["zed"];

      async.waterfall([
          function (callback) {
            self.what = "Subscribe.";

            client.call('subscribe', [{
                'url' :  "http://" + http_config.ip + ":" + http_config.port,
                'streams' : [ 'ledger' ],
              }], function (result) {
                //console.log(JSON.stringify(result, undefined, 2));

                buster.assert('ledger_index' in result);

                callback();
              });
          },
          function (callback) {
            self.what = "Accept a ledger.";

            server_events.once('request', function (request, response) {
                // console.log("GOT: %s", JSON.stringify(request, undefined, 2));

                buster.assert.equals(1, request.params.seq);
                buster.assert.equals(3, request.params.ledger_index);

                response.statusCode = 200;
                response.end(JSON.stringify({
                    jsonrpc: "2.0",
                    result: {},
                    id: request.id
                  }));

                callback();
              });

            self.remote.ledger_accept();
          },
          function (callback) {
            self.what = "Accept another ledger.";

            server_events.once('request', function (request, response) {
                // console.log("GOT: %s", JSON.stringify(request, undefined, 2));

                buster.assert.equals(2, request.params.seq);
                buster.assert.equals(4, request.params.ledger_index);

                response.statusCode = 200;
                response.end(JSON.stringify({
                    jsonrpc: "2.0",
                    result: {},
                    id: request.id
                  }));

                callback();
              });

            self.remote.ledger_accept();
          },
        ], function (error) {
          buster.refute(error, self.what);
          done();
        });
    },

//      var self    = this;
//      var ledgers = 20;
//      var got_proposed;
//
//      this.remote.transaction()
//        .payment('root', 'alice', "1")
//        .on('success', function (r) {
//            // Transaction sent.
//
//            // console.log("success: %s", JSON.stringify(r));
//          })
//        .on('pending', function() {
//            // Moving ledgers along.
//            // console.log("missing: %d", ledgers);
//
//            ledgers    -= 1;
//            if (ledgers) {
//              self.remote.ledger_accept();
//            }
//            else {
//              buster.assert(false, "Final never received.");
//              done();
//            }
//          })
//        .on('lost', function () {
//            // Transaction did not make it in.
//            // console.log("lost");
//
//            buster.assert(true);
//            done();
//          })
//        .on('proposed', function (m) {
//            // Transaction got an error.
//            // console.log("proposed: %s", JSON.stringify(m));
//
//            buster.assert.equals(m.result, 'tecNO_DST_INSUF_XRP');
//
//            got_proposed  = true;
//
//            self.remote.ledger_accept();    // Move it along.
//          })
//        .on('final', function (m) {
//            // console.log("final: %s", JSON.stringify(m, undefined, 2));
//
//            buster.assert.equals(m.metadata.TransactionResult, 'tecNO_DST_INSUF_XRP');
//            done();
//          })
//        .on('error', function(m) {
//            // console.log("error: %s", m);
//
//            buster.assert(false);
//          })
//        .submit();
});
