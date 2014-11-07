var assert    = require('assert');
var Remote    = require('ripple-lib').Remote;
var testutils = require('./testutils.js');
var config    = testutils.init_config();

suite('Remote functions', function() {
  var $ = { };

  setup(function(done) {
    testutils.build_setup().call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test('request_ledger with ledger index', function(done) {
    var request = $.remote.request_ledger();
    request.ledger_index(3);
    request.callback(function(err, m) {
      assert(!err);
      assert.strictEqual(m.ledger.ledger_index, '3');
      done();
    });
  });

  test('request_ledger with ledger index string', function(done) {
    var request = $.remote.request_ledger();
    request.ledger_index('3');
    request.callback(function(err, m) {
      assert(err);
      assert.strictEqual(err.error, 'remoteError');
      assert.strictEqual(err.remote.error, 'invalidParams');
      done();
    });
  });

  test('request_ledger with ledger identifier', function(done) {
    var request = $.remote.request_ledger();
    request.ledger_index('current');
    request.callback(function(err, m) {
      assert(!err);
      assert.strictEqual(m.ledger.ledger_index, '3');
      done();
    });
  });

  test('request_ledger_current', function(done) {
    $.remote.request_ledger_current(function(err, m) {
      assert(!err);
      assert.strictEqual(m.ledger_current_index, 3);
      done();
    });
  });

  test('request_ledger_hash', function(done) {
    $.remote.request_ledger_hash(function(err, m) {
      assert(!err);
      assert.strictEqual(m.ledger_index, 2);
      done();
    })
  });

  test('manual account_root success', function(done) {
    var self = this;

    $.remote.request_ledger_hash(function(err, r) {
      //console.log('result: %s', JSON.stringify(r));
      assert(!err);
      assert('ledger_hash' in r, 'Result missing property "ledger_hash"');

      var request = $.remote.request_ledger_entry('account_root')
      .ledger_hash(r.ledger_hash)
      .account_root('rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');

      request.callback(function(err, r) {
        // console.log('account_root: %s', JSON.stringify(r));
        assert(!err);
        assert('node' in r, 'Result missing property "node"');
        done();
      });
    });
  });

  test('account_root remote malformedAddress', function(done) {
    var self = this;

    $.remote.request_ledger_hash(function(err, r) {
      // console.log('result: %s', JSON.stringify(r));
      assert(!err);

      var request = $.remote.request_ledger_entry('account_root')
      .ledger_hash(r.ledger_hash)
      .account_root('zHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');

      request.callback(function(err, r) {
        // console.log('account_root: %s', JSON.stringify(r));
        assert(err);
        assert.strictEqual(err.error, 'remoteError');
        assert.strictEqual(err.remote.error, 'malformedAddress');
        done();
      });
    })
  });

  test('account_root entryNotFound', function(done) {
    var self = this;

    $.remote.request_ledger_hash(function(err, r) {
      // console.log('result: %s', JSON.stringify(r));
      assert(!err);

      var request = $.remote.request_ledger_entry('account_root')
      .ledger_hash(r.ledger_hash)
      .account_root('alice');

      request.callback(function(err, r) {
        // console.log('error: %s', m);
        assert(err);
        assert.strictEqual(err.error, 'remoteError');
        assert.strictEqual(err.remote.error, 'entryNotFound');
        done();
      });
    })
  });

  test('ledger_entry index', function(done) {
    var self = this;

    $.remote.request_ledger_hash(function(err, r) {
      assert(!err);

      var request = $.remote.request_ledger_entry('index')
      .ledger_hash(r.ledger_hash)
      .account_root('alice')
      .index('2B6AC232AA4C4BE41BF49D2459FA4A0347E1B543A4C92FCEE0821C0201E2E9A8');

      request.callback(function(err, r) {
        assert(!err);
        assert('node_binary' in r, 'Result missing property "node_binary"');
        done();
      });
    })
  });

  test('create account', function(done) {
    var self = this;

    var root_id = $.remote.account('root')._account_id;

    $.remote.request_subscribe().accounts(root_id).request();

    $.remote.transaction()
    .payment('root', 'alice', '10000.0')
    .once('error', done)
    .once('proposed', function(res) {
      //console.log('Submitted', res);
      $.remote.ledger_accept();
    })
    .once('success', function (r) {
      //console.log('account_root: %s', JSON.stringify(r));
      // Need to verify account and balance.
      done();
    })
    .submit();
  });

  test('create account final', function(done) {
    var self = this;
    var got_proposed;
    var got_success;

    var root_id = $.remote.account('root')._account_id;

    $.remote.request_subscribe().accounts(root_id).request();

    var transaction = $.remote.transaction()
    .payment('root', 'alice', '10000.0')

    transaction.once('submitted', function (m) {
      // console.log('proposed: %s', JSON.stringify(m));
      // buster.assert.equals(m.result, 'terNO_DST_INSUF_XRP');
      assert.strictEqual(m.engine_result, 'tesSUCCESS');
    });

    transaction.submit(done);

    testutils.ledger_wait($.remote, transaction);
  });
});

//vim:sw=2:sts=2:ts=8:et
