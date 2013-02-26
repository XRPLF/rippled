var buster      = require("buster");

var Seed        = require("../src/js/seed").Seed;

var config      = require('../src/js/config').load(require('./config'));

buster.testCase("Base58", {
  "Seed" : {
    "saESc82Vun7Ta5EJRzGJbrXb5HNYk" : function () {
      var seed = Seed.from_json("saESc82Vun7Ta5EJRzGJbrXb5HNYk");
      buster.assert.equals(seed.to_hex(), "FF1CF838D02B2CF7B45BAC27F5F24F4F");
    },
    "sp6iDHnmiPN7tQFHm5sCW59ax3hfE" : function () {
      var seed = Seed.from_json("sp6iDHnmiPN7tQFHm5sCW59ax3hfE");
      buster.assert.equals(seed.to_hex(), "00AD8DA764C3C8AF5F9B8D51C94B9E49");
    }
  }
});

// vim:sw=2:sts=2:ts=8:et
