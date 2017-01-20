/**
   This is a sample ripple npm integration test intended to be copied as a basis
   for new npm tests.
*/

// These three lines are required to initialize any test suite.
var async       = require('async');
var testutils   = require('./testutils');
var config      = testutils.init_config();

// Delete any of these next variables that aren't used in the test.
var Account     = require('ripple-lib').UInt160;
var Amount      = require('ripple-lib').Amount;
var Currency    = require('ripple-lib').UInt160;
var Remote      = require('ripple-lib').Remote;
var Server      = require('./server').Server;
var Transaction = require('ripple-lib').Transaction;
var assert      = require('assert');
var extend      = require('extend');
var fs          = require('fs');
var http        = require('http');
var path        = require('path');

suite('Sample test suite', function() {
  var $ = {};
  var opts = {};

  setup(function(done) {
    testutils.build_setup(opts).call($, done);
  });

  teardown(function(done) {
    testutils.build_teardown().call($, done);
  });

  test('Sample test', function (done) {
    var self = this;

    var steps = [
      function stepOne(callback) {
          self.what = 'Step one of the sample test';
          assert(true);
          callback();
      },

      function stepTwo(callback) {
          self.what = 'Step two of the sample test';
          assert(true);
          callback();
      },
    ];

    async.waterfall(steps, function (error) {
      assert(!error, self.what + ': ' + error);
      done();
    });
  });
});
