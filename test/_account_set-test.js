var async       = require("async");
var buster      = require("buster");

var Amount      = require("ripple-lib").Amount;
var Remote      = require("ripple-lib").Remote;
var Request     = require("ripple-lib").Request;
var Server      = require("./server").Server;

var testutils   = require("./testutils");
var config      = testutils.init_config();

// How long to wait for server to start.
var serverDelay = 1500;

buster.testRunner.timeout = 5000;

buster.testCase("AccountSet", {
  'setUp'     : testutils.build_setup(),
  // 'setUp'     : testutils.build_setup({verbose: true , no_server: false}),
  'tearDown'  : testutils.build_teardown(),

  "RequireDestTag" : function (done) {
    var self = this;

    async.waterfall([
                    function (callback) {
      self.what = "Set RequireDestTag.";

      self.remote.transaction()
      .account_set("root")
      .set_flags('RequireDestTag')
      .on('submitted', function (m) {
        //console.log("proposed: %s", JSON.stringify(m));

        callback(m.engine_result !== 'tesSUCCESS');
      })
      .submit();
    },
    function (callback) {
      self.what = "Check RequireDestTag";

      self.remote.request_account_flags('root', 'CURRENT')
      .on('success', function (m) {
        var wrong = !(m.node.Flags & Remote.flags.account_root.RequireDestTag);

        if (wrong)
          console.log("Set RequireDestTag: failed: %s", JSON.stringify(m));

        callback(wrong);
      })
      .request();
    },
    function (callback) {
      self.what = "Clear RequireDestTag.";

      self.remote.transaction()
      .account_set("root")
      .set_flags('OptionalDestTag')
      .on('submitted', function (m) {
        //console.log("proposed: %s", JSON.stringify(m));

        callback(m.engine_result !== 'tesSUCCESS');
      })
      .submit();
    },
    function (callback) {
      self.what = "Check No RequireDestTag";

      self.remote.request_account_flags('root', 'CURRENT')
      .on('success', function (m) {
        var wrong = !!(m.node.Flags & Remote.flags.account_root.RequireDestTag);

        if (wrong)
          console.log("Clear RequireDestTag: failed: %s", JSON.stringify(m));

        callback(wrong);
      })
      .request();
    },
    ], function (error) {
      buster.refute(error, self.what);
      done();
    });
  },

  "RequireAuth" : function (done) {
    var self = this;

    async.waterfall([
                    function (callback) {
      self.what = "Set RequireAuth.";

      self.remote.transaction()
      .account_set("root")
      .set_flags('RequireAuth')
      .on('submitted', function (m) {
        //console.log("proposed: %s", JSON.stringify(m));

        callback(m.engine_result !== 'tesSUCCESS');
      })
      .submit();
    },
    function (callback) {
      self.what = "Check RequireAuth";

      self.remote.request_account_flags('root', 'CURRENT')
      .on('success', function (m) {
        var wrong = !(m.node.Flags & Remote.flags.account_root.RequireAuth);

        if (wrong)
          console.log("Set RequireAuth: failed: %s", JSON.stringify(m));

        callback(wrong);
      })
      .request();
    },
    function (callback) {
      self.what = "Clear RequireAuth.";

      self.remote.transaction()
      .account_set("root")
      .set_flags('OptionalAuth')
      .on('submitted', function (m) {
        //console.log("proposed: %s", JSON.stringify(m));

        callback(m.engine_result !== 'tesSUCCESS');
      })
      .submit();
    },
    function (callback) {
      self.what = "Check No RequireAuth";

      self.remote.request_account_flags('root', 'CURRENT')
      .on('success', function (m) {
        var wrong = !!(m.node.Flags & Remote.flags.account_root.RequireAuth);

        if (wrong)
          console.log("Clear RequireAuth: failed: %s", JSON.stringify(m));

        callback(wrong);
      })
      .request();
    },
    // XXX Also check fails if something is owned.
    ], function (error) {
      buster.refute(error, self.what);
      done();
    });
  },

  "DisallowXRP" : function (done) {
    var self = this;

    async.waterfall([
                    function (callback) {
      self.what = "Set DisallowXRP.";

      self.remote.transaction()
      .account_set("root")
      .set_flags('DisallowXRP')
      .on('submitted', function (m) {
        //console.log("proposed: %s", JSON.stringify(m));

        callback(m.engine_result !== 'tesSUCCESS');
      })
      .submit();
    },
    function (callback) {
      self.what = "Check DisallowXRP";

      self.remote.request_account_flags('root', 'CURRENT')
      .on('success', function (m) {
        var wrong = !(m.node.Flags & Remote.flags.account_root.DisallowXRP);

        if (wrong)
          console.log("Set RequireDestTag: failed: %s", JSON.stringify(m));

        callback(wrong);
      })
      .request();
    },
    function (callback) {
      self.what = "Clear DisallowXRP.";

      self.remote.transaction()
      .account_set("root")
      .set_flags('AllowXRP')
      .on('submitted', function (m) {
        //console.log("proposed: %s", JSON.stringify(m));

        callback(m.engine_result !== 'tesSUCCESS');
      })
      .submit();
    },
    function (callback) {
      self.what = "Check AllowXRP";

      self.remote.request_account_flags('root', 'CURRENT')
      .on('success', function (m) {
        var wrong = !!(m.node.Flags & Remote.flags.account_root.DisallowXRP);

        if (wrong)
          console.log("Clear DisallowXRP: failed: %s", JSON.stringify(m));

        callback(wrong);
      })
      .request();
    },
    ], function (error) {
      buster.refute(error, self.what);
      done();
    });
  },

});

// vim:sw=2:sts=2:ts=8:et
