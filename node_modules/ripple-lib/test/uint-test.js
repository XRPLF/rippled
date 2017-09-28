'use strict';

/* eslint-disable max-len */

const assert = require('assert-diff');
const lodash = require('lodash');
const ripple = require('ripple-lib');
const fixtures = require('./fixtures/uint');

function resultError(test, result) {
  function type(e) {
    return Object.prototype.toString.call(e);
  }
  return `Expected ${type(test.input)}: ${test.input} to yield ${type(test.expected)}: ${test.expected === 'null' ? NaN : test.expected}. Actual: ${type(result)}: ${result}`;
}

function makeTests(uIntType) {
  describe(uIntType, function() {
    const rippleType = ripple[uIntType];
    const tests = fixtures[uIntType];

    it('from_json().to_json()', function() {
      tests['from_json().to_json()'].forEach(function(test) {
        let result = rippleType.from_json(test.input);
        assert.strictEqual(result.is_valid(), String(test.expected) !== 'null', `Validity check failed: ${test.input}`);
        result = result.to_json();

        if (test.expected === 'null') {
          // XXX
          // UInt160.to_json() returns NaN rather than null if input is invalid
          assert.strictEqual(lodash.isNaN(result), true, resultError(test, result));
        } else {
          assert.strictEqual(result, test.expected, resultError(test, result));
        }
      });
    });
    it('from_json().to_bytes()', function() {
      tests['from_json().to_bytes()'].forEach(function(test) {
        const result = rippleType.from_json(test.input);
        assert.strictEqual(result.is_valid(), String(test.expected) !== 'null', `Validity check failed: ${test.input}`);
        assert.deepEqual(result.to_bytes(), test.expected, resultError(test, result));
      });
    });
    it('from_number().to_json()', function() {
      tests['from_number().to_json()'].forEach(function(test) {
        let result = rippleType.from_number(test.input);
        assert.strictEqual(result.is_valid(), String(test.expected) !== 'null', `Validity check failed: ${test.input}`);
        result = result.to_json();

        if (test.expected === 'null') {
          // XXX
          // UInt160.to_json() returns NaN rather than null if input is invalid
          assert.strictEqual(lodash.isNaN(result), true, resultError(test, result));
        } else {
          assert.strictEqual(result, test.expected, resultError(test, result));
        }
      });
    });
    it('from_number().to_hex()', function() {
      tests['from_number().to_hex()'].forEach(function(test) {
        const result = rippleType.from_number(test.input);
        assert.strictEqual(result.is_valid(), String(test.expected) !== 'null', `Validity check failed: ${test.input}`);
        assert.strictEqual(result.to_hex(), test.expected, resultError(test, result));
      });
    });
    it('from_generic().to_*()', function() {
      tests['from_generic().to_*()'].forEach(function(test) {
        let result = rippleType.from_generic(test.input);

        switch (test.input) {
          // XXX
          // from_generic() accepts these as "zero"
          case 0:
          case '0':
          case undefined:
            switch (test.outputMethod) {
              case 'to_bytes':
                test.expected = Array(rippleType.width).fill(0);
                break;
              case 'to_json':
              case 'to_hex':
                test.expected = Array(rippleType.width * 2).fill(0).join('');
                break;
            }
        }

        assert.strictEqual(result.is_valid(), String(test.expected) !== 'null',
                           `Validity check failed: ${test.input} > ${test.expected}`);

        result = result[test.outputMethod]();

        if (test.expected === 'null') {
          // XXX
          // UInt160.to_json() returns NaN rather than null if input is invalid
          assert.strictEqual(lodash.isNaN(result), true, resultError(test, result));
        } else {
          assert.deepEqual(result, test.expected, resultError(test, result));
        }
      });
    });
  });
}

['UInt128', 'UInt160', 'UInt256'].forEach(makeTests);
