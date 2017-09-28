'use strict';
var assert = require('assert-diff');
var fixture = require('./fixtures/payment-iou-multipath.json');
var getAffectedAccounts = require('../src').getAffectedAccounts;

var accounts = [
  'r4nmQNH4Fhjfh6cHDbvVSsBv7KySbj4cBf',
  'rrnsYgWn13Z28GtRgznrSUsLfMkvsXCZSu',
  'rJsaPnGdeo7BhMnHjuc3n44Mf7Ra1qkSVJ',
  'rGpeQzUWFu4fMhJHZ1Via5aqFC3A5twZUD',
  'rnYDWQaRdMb5neCGgvFfhw3MBoxmv5LtfH'
];

describe('getAffectedAccounts', function() {
  it('Multipath payment', function() {
    var result = getAffectedAccounts(fixture.result.meta);
    assert.deepEqual(result, accounts);
  });
});
