var buster  = require("buster");

var amount  = require("../js/amount.js");
var Amount  = require("../js/amount.js").Amount;
var UInt160  = require("../js/amount.js").UInt160;

buster.testCase("Amount", {
  "UInt160" : {
    "Parse 0" : function () {
      buster.assert.equals(0, UInt160.from_json("0").value);
    },
    "Parse 0 export" : function () {
      buster.assert.equals(amount.consts.hex_xns, UInt160.from_json("0").to_json());
    },
  },
  "Amount parsing" : {
    "Parse native 0" : function () {
      buster.assert.equals("0/XNS", Amount.from_json("0").to_text_full());
    },
    "Parse native 0.0" : function () {
      buster.assert.equals("0/XNS", Amount.from_json("0.0").to_text_full());
    },
    "Parse native -0" : function () {
      buster.assert.equals("0/XNS", Amount.from_json("-0").to_text_full());
    },
    "Parse native -0.0" : function () {
      buster.assert.equals("0/XNS", Amount.from_json("-0.0").to_text_full());
    },
    "Parse native 1000" : function () {
      buster.assert.equals("1000/XNS", Amount.from_json("1000").to_text_full());
    },
    "Parse native 12.3" : function () {
      buster.assert.equals("12300000/XNS", Amount.from_json("12.3").to_text_full());
    },
    "Parse native -12.3" : function () {
      buster.assert.equals("-12300000/XNS", Amount.from_json("-12.3").to_text_full());
    },
    "Parse 123./USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh" : function () {
      buster.assert.equals("123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("123./USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").to_text_full());
    },
    "Parse 12300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh" : function () {
      buster.assert.equals("12300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("12300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").to_text_full());
    },
    "Parse 12.3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh" : function () {
      buster.assert.equals("12.3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("12.3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").to_text_full());
    },
    "Parse 1.2300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh" : function () {
      buster.assert.equals("1.23/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("1.2300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").to_text_full());
    },
    "Parse -0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh" : function () {
      buster.assert.equals("0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("-0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").to_text_full());
    },
    "Parse -0.0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh" : function () {
      buster.assert.equals("0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("-0.0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").to_text_full());
    },
  },
  "Amount operations" : {
    "Negate native 123" : function () {
      buster.assert.equals("-123/XNS", Amount.from_json("123").negate().to_text_full());
    },
    "Negate native -123" : function () {
      buster.assert.equals("123/XNS", Amount.from_json("-123").negate().to_text_full());
    },
    "Negate non-native 123" : function () {
      buster.assert.equals("-123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").negate().to_text_full());
    },
    "Negate non-native -123" : function () {
      buster.assert.equals("123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("-123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").negate().to_text_full());
    },
  }
});

// vim:sw=2:sts=2:ts=8
