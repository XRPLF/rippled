var async        = require("async");
var assert       = require('assert');
var http         = require("http");
var jsonrpc      = require("simple-jsonrpc");
var EventEmitter = require('events').EventEmitter;
var Remote       = require("ripple-lib").Remote;
var testutils    = require("./testutils");
var config       = testutils.init_config();

function build_setup(options) {
  var setup = testutils.build_setup(options);

  return function (done) {
    var self  = this;

    var http_config = config.http_servers["zed"];

    self.server_events = new EventEmitter;

    self.server = http.createServer(function (req, res) {
      // console.log("REQUEST");
      var input = "";

      req.setEncoding('utf8');

      req.on('data', function (buffer) {
        // console.log("DATA: %s", buffer);
        input = input + buffer;
      });

      req.on('end', function () {
        var request = JSON.parse(input);
        // console.log("REQ: %s", JSON.stringify(request, undefined, 2));
        self.server_events.emit('request', request, res);
      });

      req.on('close', function () { });
    });

    self.server.listen(http_config.port, http_config.ip, void(0), function () {
      // console.log("server up: %s %d", http_config.ip, http_config.port);
      setup.call(self, done);
    });
  };
};

function build_teardown() {
  var teardown = testutils.build_teardown();

  return function (done) {
    var self  = this;

    self.server.close(function () {
      // console.log("server closed");

      teardown.call(self, done);
    });
  };
};

suite('ACCOUNT_OBJECTS', function() {
  var $ = { };

  setup(function(done) {
    build_setup().call($, done);
  });

  teardown(function(done) {
    build_teardown().call($, done);
  });

  test('account_objects', function(done) {
    var self = this;

    var rippled_config = testutils.get_server_config(config);
    var client = jsonrpc.client("http://" + rippled_config.rpc_ip + ":" +
      rippled_config.rpc_port);
    var http_config = config.http_servers["zed"];

    var steps = [
      function (callback) {
        self.what = 'Create accounts';

        testutils.create_accounts(
          $.remote,
          'root',
          '20000.0',
          [ 'mtgox', 'alice', 'bob' ],
          callback
        );
      },

      function waitLedgers(callback) {
        self.what = 'Wait ledger';

        $.remote.once('ledger_closed', function() {
          callback();
        });

        $.remote.ledger_accept();
      },

      function verifyBalance(callback) {
        self.what = 'Verify balance';

        testutils.verify_balance(
          $.remote,
          [ 'mtgox', 'alice', 'bob' ],
          '19999999988',
          callback
        );
      },

     function (callback) {
        self.what = 'Set transfer rate';

        var tx = $.remote.transaction('AccountSet', {
          account: 'mtgox'
        });

        tx.transferRate(1.1 * 1e9);

        tx.submit(function(err, m) {
          assert.ifError(err);
          assert.strictEqual(m.engine_result, 'tesSUCCESS');
          callback();
        });

        testutils.ledger_wait($.remote, tx);
      },

      function (callback) {
        self.what = 'Set limits';

        testutils.credit_limits($.remote, {
          'alice' : '1000/USD/mtgox',
          'bob' : '1000/USD/mtgox'
        },
        callback);
      },

      function (callback) {
        self.what = 'Distribute funds';

        testutils.payments($.remote, {
          'mtgox' : [ '100/USD/alice', '50/USD/bob' ]
        },
        callback);
      },      

      function (callback) {
        self.what = 'Create offer';

        // get 4000/XRP pay 10/USD : offer pays 10 USD for 4000 XRP
        var tx = $.remote.transaction('OfferCreate', {
          account: 'alice',
          taker_pays: '4000',
          taker_gets: '10/USD/mtgox'
        });

        tx.submit(function(err, m) {
          assert.ifError(err);
          assert.strictEqual(m.engine_result, 'tesSUCCESS');
          callback();
        });

        testutils.ledger_wait($.remote, tx);
      },

      function (callback) {
        self.what = "Get account objects.";

        client.call('account_objects', [{
          "account": "rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn",
          "limit": 10
        }], function (result) {
          // console.log(JSON.stringify(result, undefined, 2));
          assert(typeof result === 'object');
          
          assert('account' in result);
          assert.deepEqual(result['account'], 'rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn');

          assert('account_objects' in result);
          var expected = [{
            Balance: {
              currency: 'USD',
              issuer: 'rrrrrrrrrrrrrrrrrrrrBZbvji',
              value: '-100'
            },
            Flags: 131072,
            HighLimit: {
              currency: 'USD',
              issuer: 'rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn',
              value: '1000'
            },
            HighNode: '0000000000000000',
            LedgerEntryType: 'RippleState',
            LowLimit: {
              currency: 'USD',
              issuer: 'rGihwhaqU8g7ahwAvTq6iX5rvsfcbgZw6v',
              value: '0'
            },
            LowNode: '0000000000000000',
            PreviousTxnID: result['account_objects'][0]['PreviousTxnID'],
            PreviousTxnLgrSeq: result['account_objects'][0]['PreviousTxnLgrSeq'],
            index: 'DE9CF5B006C8EA021CAB2ED20F01FC9D3260875C885155E7FA7A4DB534E36D8A'
          }, {
            Account: 'rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn',
            BookDirectory:
              'AE0A97F385FFE42E3096BA3F98A0173090FE66A3C2482FE0570E35FA931A0000',
            BookNode: '0000000000000000',
            Flags: 0,
            LedgerEntryType: 'Offer',
            OwnerNode: '0000000000000000',
            PreviousTxnID: result['account_objects'][1]['PreviousTxnID'],
            PreviousTxnLgrSeq: result['account_objects'][1]['PreviousTxnLgrSeq'],
            Sequence: 3,
            TakerGets: {
              currency: 'USD',
              issuer: 'rGihwhaqU8g7ahwAvTq6iX5rvsfcbgZw6v',
              value: '10'
            },
            TakerPays: '4000',
            index: '2A432F386EF28151AF60885CE201CC9331FF494A163D40531A9D253C97E81D61'
          }];
        
          assert.deepEqual(result['account_objects'], expected);
          callback();
        });
      }
    ];

    async.waterfall(steps, function(error) {
      assert(!error, self.what + ': ' + error);
      done();
    });
  });
});
