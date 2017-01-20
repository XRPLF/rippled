var async       = require('async');
var assert      = require('assert-diff');
var Account     = require('ripple-lib').UInt160;
var Remote      = require('ripple-lib').Remote;
var Transaction = require('ripple-lib').Transaction;
var testutils   = require('./testutils');
var config      = testutils.init_config();

assert.options.strict = true;

suite('Order Book', function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test('Track offers', function (done) {
    var self = this;

    var steps = [
      function(callback) {
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

         testutils.verify_balances(
           $.remote,
           {
             mtgox: '19999999988',
             alice: '19999999988',
             bob: '19999999988'
           },
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
        self.what = 'Create order book';

        var ob = $.remote.createOrderBook({
          currency_pays: 'XRP',
          issuer_gets: Account.json_rewrite('mtgox'),
          currency_gets: 'USD'
        });

        ob.on('model', function(){});

        ob.getOffers(function(err, offers) {
          assert.ifError(err);

          //console.log('OFFERS', offers);

          var expected = [
            { Account: 'rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn',
              BookDirectory: 'AE0A97F385FFE42E3096BA3F98A0173090FE66A3C2482FE0570E35FA931A0000',
              BookNode: '0000000000000000',
              Flags: 0,
              LedgerEntryType: 'Offer',
              OwnerNode: '0000000000000000',
              Sequence: 3,
              TakerGets: { currency: 'USD',
                issuer: 'rGihwhaqU8g7ahwAvTq6iX5rvsfcbgZw6v',
                value: '10'
              },
              TakerPays: '4000',
              index: '2A432F386EF28151AF60885CE201CC9331FF494A163D40531A9D253C97E81D61',
              owner_funds: '100',
              is_fully_funded: true,
              quality: "400",
              taker_gets_funded: '10',
              taker_pays_funded: '4000' }
          ]

          assert.deepEqual(offers, expected);

          callback(null, ob);
        });
      },

      function (ob, callback) {
        self.what = 'Create offer';

        // get 5/USD pay 2000/XRP: offer pays 2000 XRP for 5 USD
        var tx = $.remote.transaction('OfferCreate', {
          account: 'bob',
          taker_pays: '5/USD/mtgox',
          taker_gets: '2000',
        });

        tx.submit(function(err, m) {
          assert.ifError(err);
          assert.strictEqual(m.engine_result, 'tesSUCCESS');
          callback(null, ob);
        });

        testutils.ledger_wait($.remote, tx);
      },

      function (ob, callback) {
        self.what = 'Check order book tracking';

        ob.getOffers(function(err, offers) {
          assert.ifError(err);
          //console.log('OFFERS', offers);

          var expected = [
            { Account: 'rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn',
              BookDirectory: 'AE0A97F385FFE42E3096BA3F98A0173090FE66A3C2482FE0570E35FA931A0000',
              BookNode: '0000000000000000',
              Flags: 0,
              LedgerEntryType: 'Offer',
              OwnerNode: '0000000000000000',
              Sequence: 3,
              TakerGets: 
              { currency: 'USD',
                issuer: 'rGihwhaqU8g7ahwAvTq6iX5rvsfcbgZw6v',
                value: '5' },
              quality: "400",
              TakerPays: '2000',
              index: '2A432F386EF28151AF60885CE201CC9331FF494A163D40531A9D253C97E81D61',
              owner_funds: '94.5',
              is_fully_funded: true,
              taker_gets_funded: '5',
              taker_pays_funded: '2000' }
          ]
          assert.deepEqual(offers, expected);

          callback();
        });
      },
    ];

    async.waterfall(steps, function (error) {
      assert(!error, self.what + ': ' + error);
      done();
    });
  });
});
