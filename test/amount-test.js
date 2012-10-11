var buster  = require("buster");

var amount  = require("../js/amount.js");

buster.testCase("Amount", {
  "UInt160" : {
    "Parse 0" : function () {
      buster.assert.equals(0, amount.UInt160.from_json("0").value);
    },
  }
});

// vim:sw=2:sts=2:ts=8
