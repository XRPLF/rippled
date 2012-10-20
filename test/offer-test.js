
var async   = require("async");
var buster  = require("buster");
var fs	    = require("fs");

var server = require("./server.js");
var remote = require("../js/remote.js");
var config = require("./config.js");

var Amount = require("../js/amount.js").Amount;

require("../js/amount.js").setAccounts(config.accounts);

buster.testRunner.timeout = 5000;

var alpha;

buster.testCase("Offer tests", {
  'setUp' :
    function (done) {
      server.start("alpha",
	function (e) {
	  buster.refute(e);

	  alpha   = remote.remoteConfig(config, "alpha", 'TRACE');

	  alpha
	    .once('ledger_closed', done)
	    .connect();
	  }
//	  , 'MOCK'
	);
    },

  'tearDown' :
    function (done) {
      alpha
	.on('disconnected', function () {
	    server.stop("alpha", function (e) {
	      buster.refute(e);
	      done();
	    });
	  })
	.connect(false);
    },

  "offer create then cancel in one ledger" :
    function (done) {
      var final_create;

      async.waterfall([
	  function (callback) {
	    alpha.transaction()
	      .offer_create("root", "500", "100/USD/root")
	      .on("proposed", function (m) {
		  console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
		  callback(m.result != 'tesSUCCESS', m);
		})
	      .on("final", function (m) {
		  console.log("FINAL: offer_create: %s", JSON.stringify(m));

		  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);

		  final_create	= m;
		})
	      .submit();
	  },
	  function (m, callback) {
	    alpha.transaction()
	      .offer_cancel("root", m.transaction.Sequence)
	      .on("proposed", function (m) {
		  console.log("PROPOSED: offer_cancel: %s", JSON.stringify(m));
		  callback(m.result != 'tesSUCCESS', m);
		})
	      .on("final", function (m) {
		  console.log("FINAL: offer_cancel: %s", JSON.stringify(m));

		  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);
		  buster.assert(final_create);
		  done();
		})
	      .submit();
	  },
	  function (m, callback) {
	    alpha
	      .once("ledger_closed", function (ledger_closed, ledger_closed_index) {
		  console.log("LEDGER_CLOSED: %d: %s", ledger_closed_index, ledger_closed);
		})
	      .ledger_accept();
	  }
	], function (error) {
	  console.log("result: error=%s", error);
	  buster.refute(error);

	  if (error) done();
	});
    },

  "offer_create then ledger_accept then offer_cancel then ledger_accept." :
    function (done) {
      var final_create;
      var offer_seq;

      async.waterfall([
	  function (callback) {
	    alpha.transaction()
	      .offer_create("root", "500", "100/USD/root")
	      .on("proposed", function (m) {
		  console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

		  offer_seq = m.transaction.Sequence;

		  callback(m.result != 'tesSUCCESS');
		})
	      .on("final", function (m) {
		  console.log("FINAL: offer_create: %s", JSON.stringify(m));

		  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);

		  final_create	= m;

		  callback(); 
		})
	      .submit();
	  },
	  function (callback) {
	    if (!final_create) {
	      alpha
		.once("ledger_closed", function (ledger_closed, ledger_closed_index) {
		    console.log("LEDGER_CLOSED: %d: %s", ledger_closed_index, ledger_closed);

		  })
		.ledger_accept();
	    }
	    else {
	      callback(); 
	    }
	  },
	  function (callback) {
	    console.log("CANCEL: offer_cancel: %d", offer_seq);

	    alpha.transaction()
	      .offer_cancel("root", offer_seq)
	      .on("proposed", function (m) {
		  console.log("PROPOSED: offer_cancel: %s", JSON.stringify(m));
		  callback(m.result != 'tesSUCCESS');
		})
	      .on("final", function (m) {
		  console.log("FINAL: offer_cancel: %s", JSON.stringify(m));

		  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);
		  buster.assert(final_create);

		  done();
		})
	      .submit();
	  },
	  // See if ledger_accept will crash.
	  function (callback) {
	    alpha
	      .once("ledger_closed", function (ledger_closed, ledger_closed_index) {
		  console.log("LEDGER_CLOSED: A: %d: %s", ledger_closed_index, ledger_closed);
		  callback();
		})
	      .ledger_accept();
	  },
	  function (callback) {
	    alpha
	      .once("ledger_closed", function (ledger_closed, ledger_closed_index) {
		  console.log("LEDGER_CLOSED: B: %d: %s", ledger_closed_index, ledger_closed);
		  callback();
		})
	      .ledger_accept();
	  },
	], function (error) {
	  console.log("result: error=%s", error);
	  buster.refute(error);

	  if (error) done();
	});
    },


  "new user offer_create then ledger_accept then offer_cancel then ledger_accept." :
    function (done) {
      var final_create;
      var offer_seq;

      async.waterfall([
	  function (callback) {
	    alpha.transaction()
	      .payment('root', 'alice', "1000")
	      .flags('CreateAccount')
	      .on('proposed', function (m) {
		console.log("proposed: %s", JSON.stringify(m));
		buster.assert.equals(m.result, 'tesSUCCESS');
		callback();
	      })
	      .submit()
	  },
	  function (callback) {
	    alpha.transaction()
	      .offer_create("alice", "500", "100/USD/alice")
	      .on("proposed", function (m) {
		  console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

		  offer_seq = m.transaction.Sequence;

		  callback(m.result != 'tesSUCCESS');
		})
	      .on("final", function (m) {
		  console.log("FINAL: offer_create: %s", JSON.stringify(m));

		  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);

		  final_create	= m;

		  callback(); 
		})
	      .submit();
	  },
	  function (callback) {
	    if (!final_create) {
	      alpha
		.once("ledger_closed", function (ledger_closed, ledger_closed_index) {
		    console.log("LEDGER_CLOSED: %d: %s", ledger_closed_index, ledger_closed);

		  })
		.ledger_accept();
	    }
	    else {
	      callback(); 
	    }
	  },
	  function (callback) {
	    console.log("CANCEL: offer_cancel: %d", offer_seq);

	    alpha.transaction()
	      .offer_cancel("alice", offer_seq)
	      .on("proposed", function (m) {
		  console.log("PROPOSED: offer_cancel: %s", JSON.stringify(m));
		  callback(m.result != 'tesSUCCESS');
		})
	      .on("final", function (m) {
		  console.log("FINAL: offer_cancel: %s", JSON.stringify(m));

		  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);
		  buster.assert(final_create);

		  done();
		})
	      .submit();
	  },
	  // See if ledger_accept will crash.
	  function (callback) {
	    alpha
	      .once("ledger_closed", function (ledger_closed, ledger_closed_index) {
		  console.log("LEDGER_CLOSED: A: %d: %s", ledger_closed_index, ledger_closed);
		  callback();
		})
	      .ledger_accept();
	  },
	  function (callback) {
	    alpha
	      .once("ledger_closed", function (ledger_closed, ledger_closed_index) {
		  console.log("LEDGER_CLOSED: B: %d: %s", ledger_closed_index, ledger_closed);
		  callback();
		})
	      .ledger_accept();
	  },
	], function (error) {
	  console.log("result: error=%s", error);
	  buster.refute(error);
	  if (error) done();
	});
    },

  "offer cancel past and future sequence" :
    function (done) {
      var final_create;

      async.waterfall([
	  function (callback) {
	    alpha.transaction()
	      .payment('root', 'alice', Amount.from_json("10000"))
	      .flags('CreateAccount')
	      .on("proposed", function (m) {
		  console.log("PROPOSED: CreateAccount: %s", JSON.stringify(m));
		  callback(m.result != 'tesSUCCESS', m);
		})
	      .on('error', function(m) {
		  console.log("error: %s", m);

		  buster.assert(false);
		  callback(m);
		})
	      .submit();
	  },
	  // Past sequence but wrong
	  function (m, callback) {
	    alpha.transaction()
	      .offer_cancel("root", m.transaction.Sequence)
	      .on("proposed", function (m) {
		  console.log("PROPOSED: offer_cancel past: %s", JSON.stringify(m));
		  callback(m.result != 'tesSUCCESS', m);
		})
	      .submit();
	  },
	  // Same sequence
	  function (m, callback) {
	    alpha.transaction()
	      .offer_cancel("root", m.transaction.Sequence+1)
	      .on("proposed", function (m) {
		  console.log("PROPOSED: offer_cancel same: %s", JSON.stringify(m));
		  callback(m.result != 'temBAD_SEQUENCE', m);
		})
	      .submit();
	  },
	  // Future sequence
	  function (m, callback) {
	    // After a malformed transaction, need to recover correct sequence.
	    alpha.set_account_seq("root", alpha.account_seq("root")-1);

	    alpha.transaction()
	      .offer_cancel("root", m.transaction.Sequence+2)
	      .on("proposed", function (m) {
		  console.log("ERROR: offer_cancel future: %s", JSON.stringify(m));
		  callback(m.result != 'temBAD_SEQUENCE');
		})
	      .submit();
	  },
	  // See if ledger_accept will crash.
	  function (callback) {
	    alpha
	      .once("ledger_closed", function (ledger_closed, ledger_closed_index) {
		  console.log("LEDGER_CLOSED: A: %d: %s", ledger_closed_index, ledger_closed);
		  callback();
		})
	      .ledger_accept();
	  },
	  function (callback) {
	    alpha
	      .once("ledger_closed", function (ledger_closed, ledger_closed_index) {
		  console.log("LEDGER_CLOSED: B: %d: %s", ledger_closed_index, ledger_closed);
		  callback();
		})
	      .ledger_accept();
	  },
	  function (callback) {
	    callback();
	  }
	], function (error) {
	  console.log("result: error=%s", error);
	  buster.refute(error);

	  done();
	});
    },
});
// vim:sw=2:sts=2:ts=8
