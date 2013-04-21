var buster      = require("buster");

var jsbn        = require('../src/js/jsbn');
var BigInteger  = jsbn.BigInteger;
var nbi         = jsbn.nbi;

var Amount      = require("../src/js/amount").Amount;
var UInt160     = require("../src/js/uint160").UInt160;

var config      = require('../src/js/config').load(require('./config'));

// XXX Add test cases that push XRP vs non-XRP behavior.

buster.testCase("Amount", {
  "UInt160" : {
    "Parse 0" : function () {
      buster.assert.equals(nbi(), UInt160.from_generic("0")._value);
    },
    "Parse 0 export" : function () {
      buster.assert.equals(UInt160.ACCOUNT_ZERO, UInt160.from_generic("0").to_json());
    },
    "Parse 1" : function () {
      buster.assert.equals(new BigInteger([1]), UInt160.from_generic("1")._value);
    },
    "Parse rrrrrrrrrrrrrrrrrrrrrhoLvTp export" : function () {
      buster.assert.equals(UInt160.ACCOUNT_ZERO, UInt160.from_json("rrrrrrrrrrrrrrrrrrrrrhoLvTp").to_json());
    },
    "Parse rrrrrrrrrrrrrrrrrrrrBZbvji export" : function () {
      buster.assert.equals(UInt160.ACCOUNT_ONE, UInt160.from_json("rrrrrrrrrrrrrrrrrrrrBZbvji").to_json());
    },
    "Parse mtgox export" : function () {
      buster.assert.equals(config.accounts["mtgox"].account, UInt160.from_json("mtgox").to_json());
    },

    "is_valid('rrrrrrrrrrrrrrrrrrrrrhoLvTp')" : function () {
      buster.assert(UInt160.is_valid("rrrrrrrrrrrrrrrrrrrrrhoLvTp"));
    },

    "!is_valid('rrrrrrrrrrrrrrrrrrrrrhoLvT')" : function () {
      buster.refute(UInt160.is_valid("rrrrrrrrrrrrrrrrrrrrrhoLvT"));
    },
  },

  "Amount parsing" : {
    "Parse 800/USD/mtgox" : function () {
      buster.assert.equals("800/USD/"+config.accounts["mtgox"].account, Amount.from_json("800/USD/mtgox").to_text_full());
    },
    "Parse native 0" : function () {
      buster.assert.equals("0/XRP", Amount.from_json("0").to_text_full());
    },
    "Parse native 0.0" : function () {
      buster.assert.equals("0/XRP", Amount.from_json("0.0").to_text_full());
    },
    "Parse native -0" : function () {
      buster.assert.equals("0/XRP", Amount.from_json("-0").to_text_full());
    },
    "Parse native -0.0" : function () {
      buster.assert.equals("0/XRP", Amount.from_json("-0.0").to_text_full());
    },
    "Parse native 1000" : function () {
      buster.assert.equals("1000/XRP", Amount.from_json("1000").to_text_full());
    },
    "Parse native 12.3" : function () {
      buster.assert.equals("12300000/XRP", Amount.from_json("12.3").to_text_full());
    },
    "Parse native -12.3" : function () {
      buster.assert.equals("-12300000/XRP", Amount.from_json("-12.3").to_text_full());
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
      buster.assert.equals("-123/XRP", Amount.from_json("123").negate().to_text_full());
    },
    "Negate native -123" : function () {
      buster.assert.equals("123/XRP", Amount.from_json("-123").negate().to_text_full());
    },
    "Negate non-native 123" : function () {
      buster.assert.equals("-123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").negate().to_text_full());
    },
    "Negate non-native -123" : function () {
      buster.assert.equals("123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("-123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").negate().to_text_full());
    },
    "Clone non-native -123" : function () {
      buster.assert.equals("-123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("-123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").clone().to_text_full());
    },
    "Add XRP to XRP" : function () {
      buster.assert.equals("200/XRP", Amount.from_json("150").add(Amount.from_json("50")).to_text_full());
    },
    "Add USD to USD" : function () {
      buster.assert.equals("200.52/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("150.02/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").add(Amount.from_json("50.5/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Multiply 0 XRP with 0 XRP" : function () {
      buster.assert.equals("0/XRP", Amount.from_json("0").multiply(Amount.from_json("0")).to_text_full());
    },
    "Multiply 0 USD with 0 XRP" : function () {
      buster.assert.equals("0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").multiply(Amount.from_json("0")).to_text_full());
    },
    "Multiply 0 XRP with 0 USD" : function () {
      buster.assert.equals("0/XRP", Amount.from_json("0").multiply(Amount.from_json("0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Multiply 1 XRP with 0 XRP" : function () {
      buster.assert.equals("0/XRP", Amount.from_json("1").multiply(Amount.from_json("0")).to_text_full());
    },
    "Multiply 1 USD with 0 XRP" : function () {
      buster.assert.equals("0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").multiply(Amount.from_json("0")).to_text_full());
    },
    "Multiply 1 XRP with 0 USD" : function () {
      buster.assert.equals("0/XRP", Amount.from_json("1").multiply(Amount.from_json("0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Multiply 0 XRP with 1 XRP" : function () {
      buster.assert.equals("0/XRP", Amount.from_json("0").multiply(Amount.from_json("1")).to_text_full());
    },
    "Multiply 0 USD with 1 XRP" : function () {
      buster.assert.equals("0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").multiply(Amount.from_json("1")).to_text_full());
    },
    "Multiply 0 XRP with 1 USD" : function () {
      buster.assert.equals("0/XRP", Amount.from_json("0").multiply(Amount.from_json("1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Multiply XRP with USD" : function () {
      buster.assert.equals("2000/XRP", Amount.from_json("200").multiply(Amount.from_json("10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Multiply XRP with USD" : function () {
      buster.assert.equals("200000/XRP", Amount.from_json("20000").multiply(Amount.from_json("10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Multiply XRP with USD" : function () {
      buster.assert.equals("20000000/XRP", Amount.from_json("2000000").multiply(Amount.from_json("10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Multiply XRP with USD, neg" : function () {
      buster.assert.equals("-2000/XRP", Amount.from_json("200").multiply(Amount.from_json("-10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Multiply XRP with USD, neg, frac" : function () {
      buster.assert.equals("-222000/XRP", Amount.from_json("-6000").multiply(Amount.from_json("37/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Multiply USD with USD" : function () {
      buster.assert.equals("20000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").multiply(Amount.from_json("10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Multiply USD with USD" : function () {
      buster.assert.equals("200000000000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("2000000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").multiply(Amount.from_json("100000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Multiply EUR with USD, result < 1" : function () {
      buster.assert.equals("100000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").multiply(Amount.from_json("1000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Multiply EUR with USD, neg" : function () {
      buster.assert.equals("-48000000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("-24000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").multiply(Amount.from_json("2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Multiply EUR with USD, neg, <1" : function () {
      buster.assert.equals("-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("0.1/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").multiply(Amount.from_json("-1000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Multiply EUR with XRP, factor < 1" : function () {
      buster.assert.equals("100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("0.05/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").multiply(Amount.from_json("2000")).to_text_full());
    },
    "Multiply EUR with XRP, neg" : function () {
      buster.assert.equals("-500/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").multiply(Amount.from_json("5")).to_text_full());
    },
    "Multiply EUR with XRP, neg, <1" : function () {
      buster.assert.equals("-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("-0.05/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").multiply(Amount.from_json("2000")).to_text_full());
    },
    "Multiply XRP with XRP" : function () {
      buster.assert.equals("100/XRP", Amount.from_json("10").multiply(Amount.from_json("10")).to_text_full());
    },
    "Divide XRP by USD" : function () {
      buster.assert.equals("20/XRP", Amount.from_json("200").divide(Amount.from_json("10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Divide XRP by USD" : function () {
      buster.assert.equals("2000/XRP", Amount.from_json("20000").divide(Amount.from_json("10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Divide XRP by USD" : function () {
      buster.assert.equals("200000/XRP", Amount.from_json("2000000").divide(Amount.from_json("10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Divide XRP by USD, neg" : function () {
      buster.assert.equals("-20/XRP", Amount.from_json("200").divide(Amount.from_json("-10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Divide XRP by USD, neg, frac" : function () {
      buster.assert.equals("-162/XRP", Amount.from_json("-6000").divide(Amount.from_json("37/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Divide USD by USD" : function () {
      buster.assert.equals("200/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").divide(Amount.from_json("10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Divide USD by USD, fractional" : function () {
      buster.assert.equals("57142.85714285714/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("2000000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").divide(Amount.from_json("35/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Divide USD by USD" : function () {
      buster.assert.equals("20/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("2000000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").divide(Amount.from_json("100000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Divide EUR by USD, factor < 1" : function () {
      buster.assert.equals("0.1/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").divide(Amount.from_json("1000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Divide EUR by USD, neg" : function () {
      buster.assert.equals("-12/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("-24000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").divide(Amount.from_json("2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Divide EUR by USD, neg, <1" : function () {
      buster.assert.equals("-0.1/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").divide(Amount.from_json("-1000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh")).to_text_full());
    },
    "Divide EUR by XRP, result < 1" : function () {
      buster.assert.equals("0.05/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").divide(Amount.from_json("2000")).to_text_full());
    },
    "Divide EUR by XRP, neg" : function () {
      buster.assert.equals("-20/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").divide(Amount.from_json("5")).to_text_full());
    },
    "Divide EUR by XRP, neg, <1" : function () {
      buster.assert.equals("-0.05/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", Amount.from_json("-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh").divide(Amount.from_json("2000")).to_text_full());
    }
  },
  "Amount comparisons" : {
    "0 USD == 0 USD" : function () {
      var a = Amount.from_json("0/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      var b = Amount.from_json("0/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      buster.assert(a.equals(b));
      buster.refute(a.not_equals_why(b));
    },
    "0 USD == -0 USD" : function () {
      var a = Amount.from_json("0/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      var b = Amount.from_json("-0/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      buster.assert(a.equals(b));
      buster.refute(a.not_equals_why(b));
    },
    "0 XRP == 0 XRP" : function () {
      var a = Amount.from_json("0");
      var b = Amount.from_json("0.0");
      buster.assert(a.equals(b));
      buster.refute(a.not_equals_why(b));
    },
    "0 XRP == -0 XRP" : function () {
      var a = Amount.from_json("0");
      var b = Amount.from_json("-0");
      buster.assert(a.equals(b));
      buster.refute(a.not_equals_why(b));
    },
    "10 USD == 10 USD" : function () {
      var a = Amount.from_json("10/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      var b = Amount.from_json("10/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      buster.assert(a.equals(b));
      buster.refute(a.not_equals_why(b));
    },
    "123.4567 USD == 123.4567 USD" : function () {
      var a = Amount.from_json("123.4567/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      var b = Amount.from_json("123.4567/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      buster.assert(a.equals(b));
      buster.refute(a.not_equals_why(b));
    },
    "10 XRP == 10 XRP" : function () {
      var a = Amount.from_json("10");
      var b = Amount.from_json("10");
      buster.assert(a.equals(b));
      buster.refute(a.not_equals_why(b));
    },
    "1.1 XRP == 1.1 XRP" : function () {
      var a = Amount.from_json("1.1");
      var b = Amount.from_json("11.0").ratio_human(10);
      buster.assert(a.equals(b));
      buster.refute(a.not_equals_why(b));
    },
    "0 USD == 0 USD (ignore issuer)" : function () {
      var a = Amount.from_json("0/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      var b = Amount.from_json("0/USD/rH5aWQJ4R7v4Mpyf4kDBUvDFT5cbpFq3XP");
      buster.assert(a.equals(b, true));
      buster.refute(a.not_equals_why(b, true));
    },
    "1.1 USD == 1.10 USD (ignore issuer)" : function () {
      var a = Amount.from_json("1.1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      var b = Amount.from_json("1.10/USD/rH5aWQJ4R7v4Mpyf4kDBUvDFT5cbpFq3XP");
      buster.assert(a.equals(b, true));
      buster.refute(a.not_equals_why(b, true));
    },
    // Exponent mismatch
    "10 USD != 100 USD" : function () {
      var a = Amount.from_json("10/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      var b = Amount.from_json("100/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      buster.refute(a.equals(b));
      buster.assert.equals(a.not_equals_why(b), "Non-XRP value differs.");
    },
    "10 XRP != 100 XRP" : function () {
      var a = Amount.from_json("10");
      var b = Amount.from_json("100");
      buster.refute(a.equals(b));
      buster.assert.equals(a.not_equals_why(b), "XRP value differs.");
    },
    // Mantissa mismatch
    "1 USD != 2 USD" : function () {
      var a = Amount.from_json("1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      var b = Amount.from_json("2/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      buster.refute(a.equals(b));
      buster.assert.equals(a.not_equals_why(b), "Non-XRP value differs.");
    },
    "1 XRP != 2 XRP" : function () {
      var a = Amount.from_json("1");
      var b = Amount.from_json("2");
      buster.refute(a.equals(b));
      buster.assert.equals(a.not_equals_why(b), "XRP value differs.");
    },
    "0.1 USD != 0.2 USD" : function () {
      var a = Amount.from_json("0.1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      var b = Amount.from_json("0.2/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      buster.refute(a.equals(b));
      buster.assert.equals(a.not_equals_why(b), "Non-XRP value differs.");
    },
    // Sign mismatch
    "1 USD != -1 USD" : function () {
      var a = Amount.from_json("1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      var b = Amount.from_json("-1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      buster.refute(a.equals(b));
      buster.assert.equals(a.not_equals_why(b), "Non-XRP sign differs.");
    },
    "1 XRP != -1 XRP" : function () {
      var a = Amount.from_json("1");
      var b = Amount.from_json("-1");
      buster.refute(a.equals(b));
      buster.assert.equals(a.not_equals_why(b), "XRP sign differs.");
    },
    "1 USD != 1 USD (issuer mismatch)" : function () {
      var a = Amount.from_json("1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      var b = Amount.from_json("1/USD/rH5aWQJ4R7v4Mpyf4kDBUvDFT5cbpFq3XP");
      buster.refute(a.equals(b));
      buster.assert.equals(a.not_equals_why(b), "Non-XRP issuer differs: rH5aWQJ4R7v4Mpyf4kDBUvDFT5cbpFq3XP/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
    },
    "1 USD != 1 EUR" : function () {
      var a = Amount.from_json("1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      var b = Amount.from_json("1/EUR/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      buster.refute(a.equals(b));
      buster.assert.equals(a.not_equals_why(b), "Non-XRP currency differs.");
    },
    "1 USD != 1 XRP" : function () {
      var a = Amount.from_json("1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      var b = Amount.from_json("1");
      buster.refute(a.equals(b));
      buster.assert.equals(a.not_equals_why(b), "Native mismatch.");
    },
    "1 XRP != 1 USD" : function () {
      var a = Amount.from_json("1");
      var b = Amount.from_json("1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL");
      buster.refute(a.equals(b));
      buster.assert.equals(a.not_equals_why(b), "Native mismatch.");
    }
  }
});

// vim:sw=2:sts=2:ts=8:et
