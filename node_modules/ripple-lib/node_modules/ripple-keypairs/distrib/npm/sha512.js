'use strict';

var _createClass = require('babel-runtime/helpers/create-class')['default'];

var _classCallCheck = require('babel-runtime/helpers/class-call-check')['default'];

var hashjs = require('hash.js');
var BigNum = require('bn.js');

module.exports = (function () {
  function Sha512() {
    _classCallCheck(this, Sha512);

    this.hash = hashjs.sha512();
  }

  _createClass(Sha512, [{
    key: 'add',
    value: function add(bytes) {
      this.hash.update(bytes);
      return this;
    }
  }, {
    key: 'addU32',
    value: function addU32(i) {
      return this.add([i >>> 24 & 0xFF, i >>> 16 & 0xFF, i >>> 8 & 0xFF, i & 0xFF]);
    }
  }, {
    key: 'finish',
    value: function finish() {
      return this.hash.digest();
    }
  }, {
    key: 'first256',
    value: function first256() {
      return this.finish().slice(0, 32);
    }
  }, {
    key: 'first256BN',
    value: function first256BN() {
      return new BigNum(this.first256());
    }
  }]);

  return Sha512;
})();