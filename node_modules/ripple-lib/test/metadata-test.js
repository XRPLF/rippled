var assert = require('assert');
var Meta = require('ripple-lib').Meta;

describe('Meta', function() {
  var meta = new Meta(require('./fixtures/payment-iou.json').metadata);

  function callback(el, idx, ary) {
    assert.strictEqual(meta.nodes[idx],el);
  }

  it('forEach', function() {
    meta.forEach(callback);
  });

  it('map', function() {
    meta.map(callback);
  });

  it('filter', function() {
    meta.filter(callback);
  });

  it('every', function() {
    meta.every(callback);
  });

  it('some', function() {
    meta.some(callback);
  });

  it('reduce', function() {
    meta.reduce(function(prev,curr,idx,ary) {
      assert.strictEqual(meta.nodes[idx], curr);
    }, []);
  });
});