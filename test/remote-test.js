var buster    = require("buster");

var Amount    = require("ripple-lib").Amount;
var Remote    = require("ripple-lib").Remote;
var Server    = require("./server.js").Server;

var testutils = require("./testutils.js");
var config    = testutils.init_config();

// How long to wait for server to start.
var serverDelay = 1500;   // XXX Not implemented.

buster.testRunner.timeout = 5000 * 10;

buster.testCase("Remote functions", {
  'setUp' : testutils.build_setup(),
  'tearDown' : testutils.build_teardown(),

  "request_ledger_current" : function (done) {
      this.remote.request_ledger_current().on('success', function (m) {
          buster.assert.equals(m.ledger_current_index, 3);
          done();
        })
      .on('error', function(m) {
          buster.assert(false);
        })

      .request();
    },

  "request_ledger_hash" : function (done) {
      this.remote.request_ledger_hash().on('success', function (m) {
          // console.log("result: %s", JSON.stringify(m));

          buster.assert.equals(m.ledger_index, 2);
          done();
        })
      .on('error', function(m) {
          // console.log("error: %s", m);

          buster.assert(false);
        })
      .request();
    },

  "manual account_root success" : function (done) {
      var self = this;

      this.remote.request_ledger_hash().on('success', function (r) {
          // console.log("result: %s", JSON.stringify(r));

          self.remote
            .request_ledger_entry('account_root')
            .ledger_hash(r.ledger_hash)
            .account_root("rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")
            .on('success', function (r) {
                // console.log("account_root: %s", JSON.stringify(r));

                buster.assert('node' in r);
                done();
              })
            .on('error', function(m) {
                // console.log("error: %s", m);

                buster.assert(false);
              })
            .request();
        })
      .on('error', function(m) {
          // console.log("error: %s", m);

          buster.assert(false);
        })
      .request();
    },

  // XXX This should be detected locally.
  "account_root remote malformedAddress" : function (done) {
      var self = this;

      this.remote.request_ledger_hash().on('success', function (r) {
          // console.log("result: %s", JSON.stringify(r));

          self.remote
            .request_ledger_entry('account_root')
            .ledger_hash(r.ledger_hash)
            .account_root("zHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")
            .on('success', function (r) {
                // console.log("account_root: %s", JSON.stringify(r));
                buster.assert(false);
              })
            .on('error', function(m) {
                // console.log("error: %s", m);
                buster.assert.equals(m.error, 'remoteError');
                buster.assert.equals(m.remote.error, 'malformedAddress');
                done();
              })
            .request();
        })
      .on('error', function(m) {
          // console.log("error: %s", m);

          buster.assert(false);
        })
      .request();
    },

  "account_root entryNotFound" : function (done) {
      var self = this;

      this.remote.request_ledger_hash().on('success', function (r) {
          // console.log("result: %s", JSON.stringify(r));

          self.remote
            .request_ledger_entry('account_root')
            .ledger_hash(r.ledger_hash)
            .account_root("alice")
            .on('success', function (r) {
                // console.log("account_root: %s", JSON.stringify(r));

                buster.assert(false);
              })
            .on('error', function(m) {
                // console.log("error: %s", m);

                buster.assert.equals(m.error, 'remoteError');
                buster.assert.equals(m.remote.error, 'entryNotFound');
                done();
              })
            .request();
        })
      .on('error', function(m) {
          // console.log("error: %s", m);

          buster.assert(false);
        }).request();
    },

  "ledger_entry index" : function (done) {
      var self = this;

      this.remote.request_ledger_hash().on('success', function (r) {
          // console.log("result: %s", JSON.stringify(r));

          self.remote
            .request_ledger_entry('index')
            .ledger_hash(r.ledger_hash)
            .account_root("alice")
            .index("2B6AC232AA4C4BE41BF49D2459FA4A0347E1B543A4C92FCEE0821C0201E2E9A8")
            .on('success', function (r) {
                // console.log("account_root: %s", JSON.stringify(r));

                buster.assert('node_binary' in r);
                done();
              })
            .on('error', function(m) {
                // console.log("error: %s", m);

                buster.assert(false);
              }).
            request();
        })
      .on('error', function(m) {
          // console.log(m);

          buster.assert(false);
        })
      .request();
    },

  "create account" : function (done) {
    var self = this;

    var root_id = this.remote.account('root')._account_id;
    this.remote.request_subscribe().accounts(root_id).request();

    this.remote.transaction()
      .payment('root', 'alice', "10000.0")
      .on('proposed', function(res) {
        //console.log('Submitted', res);
        self.remote.ledger_accept();
      })
      .on('success', function (r) {
          //console.log("account_root: %s", JSON.stringify(r));
          // Need to verify account and balance.
          buster.assert(true);
          done();
        })
      .on('error', function(m) {
        console.log('Error');
          console.log("error: %s", m);
          buster.assert(false);
        })
      .submit();
    },

  "create account final" : function (done) {
      var self = this;
      var got_proposed;
      var got_success;

      var root_id = this.remote.account('root')._account_id;
      this.remote.request_subscribe().accounts(root_id).request();

      this.remote.transaction()
        .payment('root', 'alice', "10000.0")
        .on('success', function (r) {
          // console.log("create_account: %s", JSON.stringify(r));
          got_success = true;
        })
        .on('error', function (m) {
          // console.log("error: %s", m);
          buster.assert(false);
        })
        .on('final', function (m) {
          // console.log("final: %s", JSON.stringify(m));
          buster.assert(got_success && got_proposed);
          done();
        })
        .on('proposed', function() {
          got_proposed = true;
          self.remote.ledger_accept();
        })
        .on('submitted', function (m) {
          // console.log("proposed: %s", JSON.stringify(m));
          // buster.assert.equals(m.result, 'terNO_DST_INSUF_XRP');
          buster.assert.equals(m.engine_result, 'tesSUCCESS');
        })
        .submit();
    },
});

// vim:sw=2:sts=2:ts=8:et
