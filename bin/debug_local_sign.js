var ripple = require('ripple-lib');

var v = {
  seed: "snoPBrXtMeMyMHUVTgbuqAfg1SUTb",
  addr: "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
};

var remote = ripple.Remote.from_config({
  "trusted" : true,
  "websocket_ip" : "127.0.0.1",
  "websocket_port" : 5006,
  "websocket_ssl" : false,
  "local_signing" : true
});

var tx_json = {
	"Account" : v.addr,
	"Amount" : "10000000",
	"Destination" : "rEu2ULPiEQm1BAL8pYzmXnNX1aFX9sCks",
	"Fee" : "10",
	"Flags" : 0,
	"Sequence" : 3,
	"TransactionType" : "Payment"

  //"SigningPubKey": '0396941B22791A448E5877A44CE98434DB217D6FB97D63F0DAD23BE49ED45173C9'
};

remote.on('connected', function () {
  var req = remote.request_sign(v.seed, tx_json);
  req.message.debug_signing = true;
  req.on('success', function (result) {
    console.log("SERVER RESULT");
    console.log(result);

    var sim = {};
    var tx = remote.transaction();
    tx.tx_json = tx_json;
    tx._secret = v.seed;
    tx.complete();
    var unsigned = tx.serialize().to_hex();
    tx.sign();

    sim.tx_blob = tx.serialize().to_hex();
    sim.tx_json = tx.tx_json;
    sim.tx_signing_hash = tx.signing_hash().to_hex();
    sim.tx_unsigned = unsigned;

    console.log("\nLOCAL RESULT");
    console.log(sim);

    remote.connect(false);
  });
  req.on('error', function (err) {
    if (err.error === "remoteError" && err.remote.error === "srcActNotFound") {
      console.log("Please fund account "+v.addr+" to run this test.");
    } else {
      console.log('error', err);
    }
    remote.connect(false);
  });
  req.request();

});
remote.connect();
