var fs      = require("fs");
var buster  = require("buster");

var utils   = require("../src/js/utils.js");

buster.testCase("Utils", {
  "hexToString and stringToHex" : {
    "Even: 123456" : function () {
      buster.assert.equals("123456", utils.stringToHex(utils.hexToString("123456")));
    },
    "Odd: 12345" : function () {
      buster.assert.equals("012345", utils.stringToHex(utils.hexToString("12345")));
    },
    "Under 10: 0" : function () {
      buster.assert.equals("00", utils.stringToHex(utils.hexToString("0")));
    },
    "Under 10: 1" : function () {
      buster.assert.equals("01", utils.stringToHex(utils.hexToString("1")));
    },
    "Empty" : function () {
      buster.assert.equals("", utils.stringToHex(utils.hexToString("")));
    }
  }
});

// vim:sw=2:sts=2:ts=8:et
