var assert = require('assert');
var Request = require('ripple-lib').Request;
var makeSuite = require('./declarative-suite').makeSuite;

makeSuite('ledger_data', {dump: 'ledger-full-40000.json'},
  {
    ledger_data: function (remote, done) {
      var limit = 20;
      // keep track of indexes we've seen
      var indexes = {};
      // keep all the ledger_data items from multiple requests in one array
      var merged = [];

      function requestData (marker, callback) {
        if (typeof marker === 'function') {
          callback = marker;
          marker = undefined;
        }
        var req = new Request(remote, 'ledger_data');
        var params = req.message;

        params.ledger_index = 'validated';
        params.marker = marker;
        params.limit = limit;
        params.binary = false;

        req.callback(function (e, m) {
          assert.equal(typeof m.ledger_index, 'number');
          assert.equal(typeof m.ledger_hash, 'string');

          // make sure we didn't get some error
          assert.ifError(e);

          // make sure we aren't getting indexes we've seen before
          m.state.forEach(function (s) {
            assert(indexes[s.index] === undefined);
            indexes[s.index] = true;
            merged.push(s);
          });

          // make another request if we have a marker
          if (m.marker) {
            // make sure our limit was honoured
            assert(m.state.length == limit);
            requestData(m.marker, callback);
          } else {
            // make sure our limit was honoured
            assert(m.state.length <= limit);
            callback();
          }
        });
      }

      requestData(function () {
        remote.request_ledger({validated: true, full: true}, function (e, m) {
          // compare our stitched together account state array with one from
          // the ledegr data command
          assert.deepEqual(merged/* .concat('watch me fail') */,
                           m.ledger.accountState);
          done();
        });
      });
    }
  }
);
