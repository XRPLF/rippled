var buster  = require("buster");

var amount  = require("../js/amount.js");

buster.testCase("Amount", {
  "UInt160" : {
    "Parse 0" : function () {
      buster.assert.equals(0, amount.UInt160.from_json("0").value);
    },
    "Parse 0 export" : function () {
      buster.assert.equals(amount.consts.hex_xns, amount.UInt160.from_json("0").to_json());
    },
    "Parse native 123" : function () {
      buster.assert.equals("123/XNS", amount.Amount.from_json("123").to_text_full());
    },
    "Parse native 12.3" : function () {
      buster.assert.equals("12300000/XNS", amount.Amount.from_json("12.3").to_text_full());
    },
    "Parse 123./USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh" : function () {
      buster.assert.equals("123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", amount.Amount.from_json("123./USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").to_text_full());
    },
    "Parse 12300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh" : function () {
      buster.assert.equals("12300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", amount.Amount.from_json("12300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").to_text_full());
    },
    "Parse 12.3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh" : function () {
      buster.assert.equals("12.3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", amount.Amount.from_json("12.3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").to_text_full());
    },
    "Parse 1.2300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh" : function () {
      buster.assert.equals("1.23/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", amount.Amount.from_json("1.2300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").to_text_full());
    },
  }
});

// vim:sw=2:sts=2:ts=8
