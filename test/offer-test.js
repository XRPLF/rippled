
var async   = require("async");
var buster  = require("buster");

var Amount = require("../js/amount.js").Amount;
var Remote  = require("../js/remote.js").Remote;
var Server  = require("./server.js").Server;

var testutils  = require("./testutils.js");

buster.testRunner.timeout = 5000;

buster.testCase("Offer tests", {
  'setUp' : testutils.build_setup(),
  'tearDown' : testutils.build_teardown(),

  "offer create then cancel in one ledger" :
    function (done) {
      var self = this;
      var final_create;

      async.waterfall([
	  function (callback) {
	    self.remote.transaction()
	      .offer_create("root", "500", "100/USD/root")
	      .on('proposed', function (m) {
		  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));
		  callback(m.result != 'tesSUCCESS', m);
		})
	      .on('final', function (m) {
		  // console.log("FINAL: offer_create: %s", JSON.stringify(m));

		  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);

		  final_create	= m;
		})
	      .submit();
	  },
	  function (m, callback) {
	    self.remote.transaction()
	      .offer_cancel("root", m.tx_json.Sequence)
	      .on('proposed', function (m) {
		  // console.log("PROPOSED: offer_cancel: %s", JSON.stringify(m));
		  callback(m.result != 'tesSUCCESS', m);
		})
	      .on('final', function (m) {
		  // console.log("FINAL: offer_cancel: %s", JSON.stringify(m));

		  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);
		  buster.assert(final_create);
		  done();
		})
	      .submit();
	  },
	  function (m, callback) {
	    self.remote
	      .once('ledger_closed', function (ledger_hash, ledger_index) {
		  // console.log("LEDGER_CLOSED: %d: %s", ledger_index, ledger_hash);
		})
	      .ledger_accept();
	  }
	], function (error) {
	  // console.log("result: error=%s", error);
	  buster.refute(error);

	  if (error) done();
	});
    },

  "offer_create then ledger_accept then offer_cancel then ledger_accept." :
    function (done) {
      var self = this;
      var final_create;
      var offer_seq;

      async.waterfall([
	  function (callback) {
	    self.remote.transaction()
	      .offer_create("root", "500", "100/USD/root")
	      .on('proposed', function (m) {
		  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

		  offer_seq = m.tx_json.Sequence;

		  callback(m.result != 'tesSUCCESS');
		})
	      .on('final', function (m) {
		  // console.log("FINAL: offer_create: %s", JSON.stringify(m));

		  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);

		  final_create	= m;

		  callback(); 
		})
	      .submit();
	  },
	  function (callback) {
	    if (!final_create) {
	      self.remote
		.once('ledger_closed', function (ledger_hash, ledger_index) {
		    // console.log("LEDGER_CLOSED: %d: %s", ledger_index, ledger_hash);

		  })
		.ledger_accept();
	    }
	    else {
	      callback(); 
	    }
	  },
	  function (callback) {
	    // console.log("CANCEL: offer_cancel: %d", offer_seq);

	    self.remote.transaction()
	      .offer_cancel("root", offer_seq)
	      .on('proposed', function (m) {
		  // console.log("PROPOSED: offer_cancel: %s", JSON.stringify(m));
		  callback(m.result != 'tesSUCCESS');
		})
	      .on('final', function (m) {
		  // console.log("FINAL: offer_cancel: %s", JSON.stringify(m));

		  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);
		  buster.assert(final_create);

		  done();
		})
	      .submit();
	  },
	  // See if ledger_accept will crash.
	  function (callback) {
	    self.remote
	      .once('ledger_closed', function (ledger_hash, ledger_index) {
		  // console.log("LEDGER_CLOSED: A: %d: %s", ledger_index, ledger_hash);
		  callback();
		})
	      .ledger_accept();
	  },
	  function (callback) {
	    self.remote
	      .once('ledger_closed', function (ledger_hash, ledger_index) {
		  // console.log("LEDGER_CLOSED: B: %d: %s", ledger_index, ledger_hash);
		  callback();
		})
	      .ledger_accept();
	  },
	], function (error) {
	  // console.log("result: error=%s", error);
	  buster.refute(error);

	  if (error) done();
	});
    },


  "new user offer_create then ledger_accept then offer_cancel then ledger_accept." :
    function (done) {
      var self = this;
      var final_create;
      var offer_seq;

      async.waterfall([
	  function (callback) {
	    self.remote.transaction()
	      .payment('root', 'alice', "1000")
	      .set_flags('CreateAccount')
	      .on('proposed', function (m) {
		// console.log("proposed: %s", JSON.stringify(m));
		buster.assert.equals(m.result, 'tesSUCCESS');
		callback();
	      })
	      .submit()
	  },
	  function (callback) {
	    self.remote.transaction()
	      .offer_create("alice", "500", "100/USD/alice")
	      .on('proposed', function (m) {
		  // console.log("PROPOSED: offer_create: %s", JSON.stringify(m));

		  offer_seq = m.tx_json.Sequence;

		  callback(m.result != 'tesSUCCESS');
		})
	      .on('final', function (m) {
		  // console.log("FINAL: offer_create: %s", JSON.stringify(m));

		  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);

		  final_create	= m;

		  callback(); 
		})
	      .submit();
	  },
	  function (callback) {
	    if (!final_create) {
	      self.remote
		.once('ledger_closed', function (ledger_hash, ledger_index) {
		    // console.log("LEDGER_CLOSED: %d: %s", ledger_index, ledger_hash);

		  })
		.ledger_accept();
	    }
	    else {
	      callback(); 
	    }
	  },
	  function (callback) {
	    // console.log("CANCEL: offer_cancel: %d", offer_seq);

	    self.remote.transaction()
	      .offer_cancel("alice", offer_seq)
	      .on('proposed', function (m) {
		  // console.log("PROPOSED: offer_cancel: %s", JSON.stringify(m));
		  callback(m.result != 'tesSUCCESS');
		})
	      .on('final', function (m) {
		  // console.log("FINAL: offer_cancel: %s", JSON.stringify(m));

		  buster.assert.equals('tesSUCCESS', m.metadata.TransactionResult);
		  buster.assert(final_create);

		  done();
		})
	      .submit();
	  },
	  // See if ledger_accept will crash.
	  function (callback) {
	    self.remote
	      .once('ledger_closed', function (ledger_hash, ledger_index) {
		  // console.log("LEDGER_CLOSED: A: %d: %s", ledger_index, ledger_hash);
		  callback();
		})
	      .ledger_accept();
	  },
	  function (callback) {
	    self.remote
	      .once('ledger_closed', function (ledger_hash, ledger_index) {
		  // console.log("LEDGER_CLOSED: B: %d: %s", ledger_index, ledger_hash);
		  callback();
		})
	      .ledger_accept();
	  },
	], function (error) {
	  // console.log("result: error=%s", error);
	  buster.refute(error);
	  if (error) done();
	});
    },

  "offer cancel past and future sequence" :
    function (done) {
      var self = this;
      var final_create;

      async.waterfall([
	  function (callback) {
	    self.remote.transaction()
	      .payment('root', 'alice', Amount.from_json("10000"))
	      .set_flags('CreateAccount')
	      .on('proposed', function (m) {
		  // console.log("PROPOSED: CreateAccount: %s", JSON.stringify(m));
		  callback(m.result != 'tesSUCCESS', m);
		})
	      .on('error', function(m) {
		  // console.log("error: %s", m);

		  buster.assert(false);
		  callback(m);
		})
	      .submit();
	  },
	  // Past sequence but wrong
	  function (m, callback) {
	    self.remote.transaction()
	      .offer_cancel("root", m.tx_json.Sequence)
	      .on('proposed', function (m) {
		  // console.log("PROPOSED: offer_cancel past: %s", JSON.stringify(m));
		  callback(m.result != 'tesSUCCESS', m);
		})
	      .submit();
	  },
	  // Same sequence
	  function (m, callback) {
	    self.remote.transaction()
	      .offer_cancel("root", m.tx_json.Sequence+1)
	      .on('proposed', function (m) {
		  // console.log("PROPOSED: offer_cancel same: %s", JSON.stringify(m));
		  callback(m.result != 'temBAD_SEQUENCE', m);
		})
	      .submit();
	  },
	  // Future sequence
	  function (m, callback) {
	    // After a malformed transaction, need to recover correct sequence.
	    self.remote.set_account_seq("root", self.remote.account_seq("root")-1);

	    self.remote.transaction()
	      .offer_cancel("root", m.tx_json.Sequence+2)
	      .on('proposed', function (m) {
		  // console.log("ERROR: offer_cancel future: %s", JSON.stringify(m));
		  callback(m.result != 'temBAD_SEQUENCE');
		})
	      .submit();
	  },
	  // See if ledger_accept will crash.
	  function (callback) {
	    self.remote
	      .once('ledger_closed', function (ledger_hash, ledger_index) {
		  // console.log("LEDGER_CLOSED: A: %d: %s", ledger_index, ledger_hash);
		  callback();
		})
	      .ledger_accept();
	  },
	  function (callback) {
	    self.remote
	      .once('ledger_closed', function (ledger_hash, ledger_index) {
		  // console.log("LEDGER_CLOSED: B: %d: %s", ledger_index, ledger_hash);
		  callback();
		})
	      .ledger_accept();
	  },
	  function (callback) {
	    callback();
	  }
	], function (error) {
	  // console.log("result: error=%s", error);
	  buster.refute(error);

	  done();
	});
    },
});
// vim:sw=2:sts=2:ts=8
