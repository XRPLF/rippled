var buster      = require("buster");

var Seed        = require("../src/js/seed").Seed;

var config      = require('../src/js/config').load(require('./config'));

buster.testCase("Signing", {
  "Keys" : {
    "SigningPubKey 1 (ripple-client issue #245)" : function () {
      var seed = Seed.from_json("saESc82Vun7Ta5EJRzGJbrXb5HNYk");
      var key = seed.get_key("rBZ4j6MsoctipM6GEyHSjQKzXG3yambDnZ");
      var pub = key.to_hex_pub();
      buster.assert.equals(pub, "0396941B22791A448E5877A44CE98434DB217D6FB97D63F0DAD23BE49ED45173C9");
    },
    "SigningPubKey 2 (master seed)" : function () {
      var seed = Seed.from_json("snoPBrXtMeMyMHUVTgbuqAfg1SUTb");
      var key = seed.get_key("rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh");
      var pub = key.to_hex_pub();
      buster.assert.equals(pub, "0330E7FC9D56BB25D6893BA3F317AE5BCF33B3291BD63DB32654A313222F7FD020");
    }
  }
});

// vim:sw=2:sts=2:ts=8:et
