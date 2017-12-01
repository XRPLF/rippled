/* eslint-disable max-len */
'use strict';
const assert = require('assert');
const Amount = require('ripple-lib').Amount;
const UInt160 = require('ripple-lib').UInt160;


describe('Amount', function() {
  describe('Negatives', function() {
    it('Number 1', function() {
      assert.strictEqual(Amount.from_human('0').add(Amount.from_human('-1')).to_human(), '-1');
    });
  });
  describe('Positives', function() {
    it('Number 1', function() {
      assert(Amount.from_json('1').is_positive());
    });
  });
  describe('Positives', function() {
    it('Number 1', function() {
      assert(Amount.from_json('1').is_positive());
    });
  });
  // also tested extensively in other cases
  describe('to_human', function() {
    it('12345.6789 XAU', function() {
      assert.strictEqual(Amount.from_human('12345.6789 XAU').to_human(), '12,345.6789');
    });
    it('12345.678901234 XAU', function() {
      assert.strictEqual(Amount.from_human('12345.678901234 XAU').to_human(), '12,345.678901234');
    });
    it('to human, precision -1, should be ignored, precision needs to be >= 0', function() {
      assert.strictEqual(Amount.from_human('12345.678901234 XAU').to_human({precision: -1}), '12,346');
    });
    it('to human, precision 0', function() {
      assert.strictEqual(Amount.from_human('12345.678901234 XAU').to_human({precision: 0}), '12,346');
    });
    it('to human, precision 1', function() {
      assert.strictEqual(Amount.from_human('12345.678901234 XAU').to_human({precision: 1}), '12,345.7');
    });
    it('to human, precision 2', function() {
      assert.strictEqual(Amount.from_human('12345.678901234 XAU').to_human({precision: 2}), '12,345.68');
    });
    it('to human, precision 3', function() {
      assert.strictEqual(Amount.from_human('12345.678901234 XAU').to_human({precision: 3}), '12,345.679');
    });
    it('to human, precision 4', function() {
      assert.strictEqual(Amount.from_human('12345.678901234 XAU').to_human({precision: 4}), '12,345.6789');
    });
    it('to human, precision 5', function() {
      assert.strictEqual(Amount.from_human('12345.678901234 XAU').to_human({precision: 5}), '12,345.67890');
    });
    it('to human, precision -1, should be ignored, precision needs to be >= 0', function() {
      assert.strictEqual(Amount.from_human('0.00012345 XAU').to_human({precision: -1}), '0');
    });
    it('to human, precision 0', function() {
      assert.strictEqual(Amount.from_human('0.00012345 XAU').to_human({precision: 0}), '0');
    });
    it('to human, precision 1', function() {
      assert.strictEqual(Amount.from_human('0.00012345 XAU').to_human({precision: 1}), '0.0');
    });
    it('to human, precision 2', function() {
      assert.strictEqual(Amount.from_human('0.00012345 XAU').to_human({precision: 2}), '0.00');
    });
    it('to human, precision 5', function() {
      assert.strictEqual(Amount.from_human('0.00012345 XAU').to_human({precision: 5}), '0.00012');
    });
    it('to human, precision 6', function() {
      assert.strictEqual(Amount.from_human('0.00012345 XAU').to_human({precision: 6}), '0.000123');
    });
    it('to human, precision 16', function() {
      assert.strictEqual(Amount.from_human('0.00012345 XAU').to_human({precision: 16}), '0.00012345');
    });
    it('to human, precision 16, min_precision 16', function() {
      assert.strictEqual(Amount.from_human('0.00012345 XAU').to_human({precision: 16, min_precision: 16}), '0.0001234500000000');
    });
    it('to human, precision 16, min_precision 12', function() {
      assert.strictEqual(Amount.from_human('0.00012345 XAU').to_human({precision: 16, min_precision: 12}), '0.000123450000');
    });
    it('to human, precision 0, first decimal 4', function() {
      assert.strictEqual(Amount.from_human('0.4 XAU').to_human({precision: 0}), '0');
    });
    it('to human, precision 0, first decimal 5', function() {
      assert.strictEqual(Amount.from_human('0.5 XAU').to_human({precision: 0}), '1');
    });
    it('to human, precision 0, first decimal 8', function() {
      assert.strictEqual(Amount.from_human('0.8 XAU').to_human({precision: 0}), '1');
    });
    it('to human, precision 0, precision 16', function() {
      assert.strictEqual(Amount.from_human('0.0 XAU').to_human({precision: 16}), '0');
    });
    it('to human, precision 0, precision 8, min_precision 16', function() {
      assert.strictEqual(Amount.from_human('0.0 XAU').to_human({precision: 8, min_precision: 16}), '0.0000000000000000');
    });
    it('to human, precision 0, first decimal 8', function() {
      assert.strictEqual(Amount.from_human('0.8 XAU').to_human({precision: 0}), '1');
    });
    it('to human, precision 6, min_precision 6, max_sig_digits 20', function() {
      assert.strictEqual(Amount.from_human('0.0 XAU').to_human({precision: 6, min_precision: 6, max_sig_digits: 20}), '0.000000');
    });
    it('to human, precision 16, min_precision 6, max_sig_digits 20', function() {
      assert.strictEqual(Amount.from_human('0.0 XAU').to_human({precision: 16, min_precision: 6, max_sig_digits: 20}), '0.000000');
    });
    it('to human rounding edge case, precision 2, 1', function() {
      assert.strictEqual(Amount.from_human('0.99 XAU').to_human({precision: 1}), '1.0');
    });
    it('to human rounding edge case, precision 2, 2', function() {
      assert.strictEqual(Amount.from_human('0.99 XAU').to_human({precision: 2}), '0.99');
    });
    it('to human rounding edge case, precision 2, 3', function() {
      assert.strictEqual(Amount.from_human('0.99 XAU').to_human({precision: 3}), '0.99');
    });
    it('to human rounding edge case, precision 2, 3 min precision 3', function() {
      assert.strictEqual(Amount.from_human('0.99 XAU').to_human({precision: 3, min_precision: 3}), '0.990');
    });
    it('to human rounding edge case, precision 3, 2', function() {
      assert.strictEqual(Amount.from_human('0.999 XAU').to_human({precision: 2}), '1.00');
    });
    it('to human very small number', function() {
      assert.strictEqual(Amount.from_json('12e-20/USD').to_human(), '0.00000000000000000012');
    });
    it('to human very small number with precision', function() {
      assert.strictEqual(Amount.from_json('12e-20/USD').to_human({precision: 20}), '0.00000000000000000012');
    });
  });
  describe('from_human', function() {
    it('empty string', function() {
      assert.strictEqual(Amount.from_human('').to_text_full(), 'NaN');
    });
    it('missing value', function() {
      assert.strictEqual(Amount.from_human('USD').to_text_full(), 'NaN');
    });
    it('1 XRP', function() {
      assert.strictEqual(Amount.from_human('1 XRP').to_text_full(), '1/XRP');
    });
    it('1 XRP human', function() {
      assert.strictEqual(Amount.from_human('1 XRP').to_human_full(), '1/XRP');
    });
    it('1XRP human', function() {
      assert.strictEqual(Amount.from_human('1XRP').to_human_full(), '1/XRP');
    });
    it('0.1 XRP', function() {
      assert.strictEqual(Amount.from_human('0.1 XRP').to_text_full(), '0.1/XRP');
    });
    it('0.1 XRP human', function() {
      assert.strictEqual(Amount.from_human('0.1 XRP').to_human_full(), '0.1/XRP');
    });
    it('0.1 USD', function() {
      assert.strictEqual(Amount.from_human('0.1 USD').to_text_full(), '0.1/USD/NaN');
    });
    it('0.1 USD human', function() {
      assert.strictEqual(Amount.from_human('0.1 USD').to_human_full(), '0.1/USD/NaN');
    });
    it('10000 USD', function() {
      assert.strictEqual(Amount.from_human('10000 USD').to_text_full(), '10000/USD/NaN');
    });
    it('10000 USD human', function() {
      assert.strictEqual(Amount.from_human('10000 USD').to_human_full(), '10,000/USD/NaN');
    });
    it('USD 10000', function() {
      assert.strictEqual(Amount.from_human('USD 10000').to_text_full(), '10000/USD/NaN');
    });
    it('USD 10000 human', function() {
      assert.strictEqual(Amount.from_human('USD 10000').to_human_full(), '10,000/USD/NaN');
    });
    it('12345.6789 XAU', function() {
      assert.strictEqual(Amount.from_human('12345.6789 XAU').to_text_full(), '12345.6789/XAU/NaN');
    });
    it('12345.6789 XAU human', function() {
      assert.strictEqual(Amount.from_human('12345.6789 XAU').to_human_full(), '12,345.6789/XAU/NaN');
    });
    it('12345.6789 015841551A748AD2C1F76FF6ECB0CCCD00000000', function() {
      assert.strictEqual(Amount.from_human('12345.6789 015841551A748AD2C1F76FF6ECB0CCCD00000000').to_text_full(), '12345.6789/XAU (-0.5%pa)/NaN');
    });
    it('12345.6789 015841551A748AD2C1F76FF6ECB0CCCD00000000 human', function() {
      assert.strictEqual(Amount.from_human('12345.6789 015841551A748AD2C1F76FF6ECB0CCCD00000000').to_human_full(), '12,345.6789/XAU (-0.5%pa)/NaN');
    });
    it('12345.6789 0000000000000000000000005553440000000000', function() {
      assert.strictEqual(Amount.from_human('12345.6789 0000000000000000000000005553440000000000').to_text_full(), '12345.6789/USD/NaN');
    });
    it('12345.6789 0000000000000000000000005553440000000000 human', function() {
      assert.strictEqual(Amount.from_human('12345.6789 0000000000000000000000005553440000000000').to_human_full(), '12,345.6789/USD/NaN');
    });
    it('10 0000000000000000000000005553440000000000', function() {
      assert.strictEqual(Amount.from_human('10 0000000000000000000000005553440000000000').to_text_full(), '10/USD/NaN');
    });
    it('10 0000000000000000000000005553440000000000 human', function() {
      assert.strictEqual(Amount.from_human('10 0000000000000000000000005553440000000000').to_human_full(), '10/USD/NaN');
    });
    it('100 0000000000000000000000005553440000000000', function() {
      assert.strictEqual(Amount.from_human('100 0000000000000000000000005553440000000000').to_text_full(), '100/USD/NaN');
    });
    it('100 0000000000000000000000005553440000000000 human', function() {
      assert.strictEqual(Amount.from_human('100 0000000000000000000000005553440000000000').to_human_full(), '100/USD/NaN');
    });
    it('1000 0000000000000000000000005553440000000000', function() {
      assert.strictEqual(Amount.from_human('1000 0000000000000000000000005553440000000000').to_text_full(), '1000/USD/NaN');
    });
    it('1000 0000000000000000000000005553440000000000 human', function() {
      assert.strictEqual(Amount.from_human('1000 0000000000000000000000005553440000000000').to_human_full(), '1,000/USD/NaN');
    });
    it('-100 0000000000000000000000005553440000000000', function() {
      assert.strictEqual(Amount.from_human('-100 0000000000000000000000005553440000000000').to_text_full(), '-100/USD/NaN');
    });
    it('-100 0000000000000000000000005553440000000000 human', function() {
      assert.strictEqual(Amount.from_human('-100 0000000000000000000000005553440000000000').to_human_full(), '-100/USD/NaN');
    });
    it('-1000 0000000000000000000000005553440000000000', function() {
      assert.strictEqual(Amount.from_human('-1000 0000000000000000000000005553440000000000').to_text_full(), '-1000/USD/NaN');
    });
    it('-1000 0000000000000000000000005553440000000000 human', function() {
      assert.strictEqual(Amount.from_human('-1000 0000000000000000000000005553440000000000').to_human_full(), '-1,000/USD/NaN');
    });
    it('-1000.001 0000000000000000000000005553440000000000', function() {
      assert.strictEqual(Amount.from_human('-1000.001 0000000000000000000000005553440000000000').to_text_full(), '-1000.001/USD/NaN');
    });
    it('-1000.001 0000000000000000000000005553440000000000 human', function() {
      assert.strictEqual(Amount.from_human('-1000.001 0000000000000000000000005553440000000000').to_human_full(), '-1,000.001/USD/NaN');
    });
    it('XAU 12345.6789', function() {
      assert.strictEqual(Amount.from_human('XAU 12345.6789').to_text_full(), '12345.6789/XAU/NaN');
    });
    it('XAU 12345.6789 human', function() {
      assert.strictEqual(Amount.from_human('XAU 12345.6789').to_human_full(), '12,345.6789/XAU/NaN');
    });
    it('101 12345.6789', function() {
      assert.strictEqual(Amount.from_human('101 12345.6789').to_text_full(), '12345.6789/101/NaN');
    });
    it('101 12345.6789 human', function() {
      assert.strictEqual(Amount.from_human('101 12345.6789').to_human_full(), '12,345.6789/101/NaN');
    });
    it('12345.6789 101', function() {
      assert.strictEqual(Amount.from_human('12345.6789 101').to_text_full(), '12345.6789/101/NaN');
    });
    it('12345.6789 101 human', function() {
      assert.strictEqual(Amount.from_human('12345.6789 101').to_human_full(), '12,345.6789/101/NaN');
    });
  });
  describe('from_json', function() {
    it('1 XRP', function() {
      assert.strictEqual(Amount.from_json('1/XRP').to_text_full(), '1/XRP/NaN');
    });
    it('1 XRP human', function() {
      assert.strictEqual(Amount.from_json('1/XRP').to_human_full(), '1/XRP/NaN');
    });
  });
  describe('from_number', function() {
    it('Number 1', function() {
      assert.strictEqual(Amount.from_number(1).to_text_full(), '1/1/rrrrrrrrrrrrrrrrrrrrBZbvji');
    });
    it('Number 1 human', function() {
      assert.strictEqual(Amount.from_number(1).to_human_full(), '1/1/rrrrrrrrrrrrrrrrrrrrBZbvji');
    });
    it('Number 2', function() {
      assert.strictEqual(Amount.from_number(2).to_text_full(), '2/1/rrrrrrrrrrrrrrrrrrrrBZbvji');
    });
    it('Number 2 human', function() {
      assert.strictEqual(Amount.from_number(2).to_human_full(), '2/1/rrrrrrrrrrrrrrrrrrrrBZbvji');
    });
    it('Multiply 2 "1" with 3 "1", by product_human', function() {
      assert.strictEqual(Amount.from_number(2).product_human(Amount.from_number(3)).to_text_full(), '6/1/rrrrrrrrrrrrrrrrrrrrBZbvji');
    });
    it('Multiply 2 "1" with 3 "1", by product_human human', function() {
      assert.strictEqual(Amount.from_number(2).product_human(Amount.from_number(3)).to_human_full(), '6/1/rrrrrrrrrrrrrrrrrrrrBZbvji');
    });
    it('Multiply 3 USD with 3 "1"', function() {
      assert.strictEqual(Amount.from_json('3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_number(3)).to_text_full(), '9/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Multiply 3 USD with 3 "1" human', function() {
      assert.strictEqual(Amount.from_json('3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_number(3)).to_human_full(), '9/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Multiply -1 "1" with 3 USD', function() {
      assert.strictEqual(Amount.from_number(-1).multiply(Amount.from_json('3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full(), '-3/1/rrrrrrrrrrrrrrrrrrrrBZbvji');
    });
    it('Multiply -1 "1" with 3 USD human', function() {
      assert.strictEqual(Amount.from_number(-1).multiply(Amount.from_json('3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full(), '-3/1/rrrrrrrrrrrrrrrrrrrrBZbvji');
    });
    it('Multiply -1 "1" with 3 USD, by product_human', function() {
      assert.strictEqual(Amount.from_number(-1).product_human(Amount.from_json('3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full(), '-3/1/rrrrrrrrrrrrrrrrrrrrBZbvji');
    });
    it('Multiply -1 "1" with 3 USD, by product_human human', function() {
      assert.strictEqual(Amount.from_number(-1).product_human(Amount.from_json('3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full(), '-3/1/rrrrrrrrrrrrrrrrrrrrBZbvji');
    });
  });
  describe('text_full_rewrite', function() {
    it('Number 1', function() {
      assert.strictEqual('0.000001/XRP', Amount.text_full_rewrite(1));
    });
  });
  describe('json_rewrite', function() {
    it('Number 1', function() {
      assert.strictEqual('1', Amount.json_rewrite(1));
    });
  });
  describe('UInt160', function() {
    it('Parse 0 export', function() {
      assert.strictEqual(UInt160.ACCOUNT_ZERO, UInt160.from_generic('0').set_version(0).to_json());
    });
    it('Parse 1', function() {
      assert.deepEqual(UInt160.ACCOUNT_ONE, UInt160.from_generic('1').set_version(0).to_json());
    });
    it('Parse rrrrrrrrrrrrrrrrrrrrrhoLvTp export', function() {
      assert.strictEqual(UInt160.ACCOUNT_ZERO, UInt160.from_json('rrrrrrrrrrrrrrrrrrrrrhoLvTp').to_json());
    });
    it('Parse rrrrrrrrrrrrrrrrrrrrBZbvji export', function() {
      assert.strictEqual(UInt160.ACCOUNT_ONE, UInt160.from_json('rrrrrrrrrrrrrrrrrrrrBZbvji').to_json());
    });
    it('is_valid rrrrrrrrrrrrrrrrrrrrrhoLvTp', function() {
      assert(UInt160.is_valid('rrrrrrrrrrrrrrrrrrrrrhoLvTp'));
    });
    it('!is_valid rrrrrrrrrrrrrrrrrrrrrhoLvT', function() {
      assert(!UInt160.is_valid('rrrrrrrrrrrrrrrrrrrrrhoLvT'));
    });
  });
  describe('Amount validity', function() {
    it('is_valid 1', function() {
      assert(Amount.is_valid(1));
    });
    it('is_valid "1"', function() {
      assert(Amount.is_valid('1'));
    });
    it('is_valid "1/XRP"', function() {
      assert(Amount.is_valid('1/XRP'));
    });
    it('!is_valid NaN', function() {
      assert(!Amount.is_valid(NaN));
    });
    it('!is_valid "xx"', function() {
      assert(!Amount.is_valid('xx'));
    });
    it('!is_valid_full 1', function() {
      assert(!Amount.is_valid_full(1));
    });
    it('is_valid_full "1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL"', function() {
      assert(Amount.is_valid_full('1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL'));
    });
  });
  describe('Amount parsing', function() {
    it('Parse invalid string', function() {
      assert.strictEqual(Amount.from_json('x').to_text(), 'NaN');
      assert.strictEqual(typeof Amount.from_json('x').to_text(), 'string');
      assert(isNaN(Amount.from_json('x').to_text()));
    });
    it('parse dem', function() {
      assert.strictEqual(Amount.from_json('10/015841551A748AD2C1F76FF6ECB0CCCD00000000/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_text_full(), '10/XAU (-0.5%pa)/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('parse dem human', function() {
      assert.strictEqual(Amount.from_json('10/015841551A748AD2C1F76FF6ECB0CCCD00000000/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_human_full(), '10/XAU (-0.5%pa)/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('parse dem', function() {
      assert.strictEqual(Amount.from_json('10/XAU (-0.5%pa)/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_text_full(), '10/XAU (-0.5%pa)/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('parse dem human', function() {
      assert.strictEqual(Amount.from_json('10/XAU (-0.5%pa)/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_human_full(), '10/XAU (-0.5%pa)/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Parse native 0', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').to_text_full());
    });
    it('Parse native 0.0', function() {
      assert.throws(function() {
        Amount.from_json('0.0');
      });
    });
    it('Parse native -0', function() {
      assert.strictEqual('0/XRP', Amount.from_json('-0').to_text_full());
    });
    it('Parse native -0.0', function() {
      assert.throws(function() {
        Amount.from_json('-0.0');
      });
    });
    it('Parse native 1000', function() {
      assert.strictEqual('0.001/XRP', Amount.from_json('1000').to_text_full());
    });
    it('Parse native 12300000', function() {
      assert.strictEqual('12.3/XRP', Amount.from_json('12300000').to_text_full());
    });
    it('Parse native -12300000', function() {
      assert.strictEqual('-12.3/XRP', Amount.from_json('-12300000').to_text_full());
    });
    it('Parse 123./USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      assert.strictEqual('123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('123./USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_text_full());
    });
    it('Parse 12300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      assert.strictEqual('12300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('12300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_text_full());
    });
    it('Parse 12.3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      assert.strictEqual('12.3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('12.3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_text_full());
    });
    it('Parse 1.2300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      assert.strictEqual('1.23/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('1.2300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_text_full());
    });
    it('Parse -0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      assert.strictEqual('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_text_full());
    });
    it('Parse -0.0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      assert.strictEqual('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-0.0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_text_full());
    });
    it('Parse 0.0/111/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      assert.strictEqual('0/111/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('0/111/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_text_full());
    });
    it('Parse 0.0/12D/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      assert.strictEqual('0/12D/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('0/12D/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_text_full());
    });
    it('Parse native 0 human', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').to_human_full());
    });
    it('Parse native -0 human', function() {
      assert.strictEqual('0/XRP', Amount.from_json('-0').to_human_full());
    });
    it('Parse native 1000 human', function() {
      assert.strictEqual('0.001/XRP', Amount.from_json('1000').to_human_full());
    });
    it('Parse native 12300000 human', function() {
      assert.strictEqual('12.3/XRP', Amount.from_json('12300000').to_human_full());
    });
    it('Parse native -12300000 human', function() {
      assert.strictEqual('-12.3/XRP', Amount.from_json('-12300000').to_human_full());
    });
    it('Parse 123./USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh human', function() {
      assert.strictEqual('123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('123./USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_human_full());
    });
    it('Parse 12300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh human', function() {
      assert.strictEqual('12,300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('12300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_human_full());
    });
    it('Parse 12.3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh human', function() {
      assert.strictEqual('12.3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('12.3/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_human_full());
    });
    it('Parse 1.2300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh human', function() {
      assert.strictEqual('1.23/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('1.2300/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_human_full());
    });
    it('Parse -0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh human', function() {
      assert.strictEqual('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_human_full());
    });
    it('Parse -0.0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh human', function() {
      assert.strictEqual('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-0.0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_human_full());
    });
    it('Parse 0.0/111/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh human', function() {
      assert.strictEqual('0/111/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('0/111/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_human_full());
    });
    it('Parse 0.0/12D/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh human', function() {
      assert.strictEqual('0/12D/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('0/12D/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').to_human_full());
    });
  });
  describe('Amount to_json', function() {
    it('10 USD', function() {
      const amount = Amount.from_human('10 USD').to_json();
      assert.strictEqual('10', amount.value);
      assert.strictEqual('USD', amount.currency);
    });
    it('10 0000000000000000000000005553440000000000', function() {
      const amount = Amount.from_human('10 0000000000000000000000005553440000000000').to_json();
      assert.strictEqual('10', amount.value);
      assert.strictEqual('USD', amount.currency);
    });
    it('10 015841551A748AD2C1F76FF6ECB0CCCD00000000', function() {
      const amount = Amount.from_human('10 015841551A748AD2C1F76FF6ECB0CCCD00000000').to_json();
      assert.strictEqual('10', amount.value);
      assert.strictEqual('015841551A748AD2C1F76FF6ECB0CCCD00000000', amount.currency);
    });
  });
  describe('Amount operations', function() {
    it('Negate native 123', function() {
      assert.strictEqual('-0.000123/XRP', Amount.from_json('123').negate().to_text_full());
    });
    it('Negate native -123', function() {
      assert.strictEqual('0.000123/XRP', Amount.from_json('-123').negate().to_text_full());
    });
    it('Negate non-native 123', function() {
      assert.strictEqual('-123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').negate().to_text_full());
    });
    it('Negate non-native -123', function() {
      assert.strictEqual('123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').negate().to_text_full());
    });
    it('Clone non-native -123', function() {
      assert.strictEqual('-123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').clone().to_text_full());
    });
    it('Add XRP to XRP', function() {
      assert.strictEqual('0.0002/XRP', Amount.from_json('150').add(Amount.from_json('50')).to_text_full());
    });
    it('Add USD to USD', function() {
      assert.strictEqual('200.52/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('150.02/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').add(Amount.from_json('50.5/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Add 0 USD to 1 USD', function() {
      assert.strictEqual('1', Amount.from_json('1/USD').add('0/USD').to_text());
    });
    it('Subtract USD from USD', function() {
      assert.strictEqual('99.52/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('150.02/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').subtract(Amount.from_json('50.5/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply 0 XRP with 0 XRP', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').multiply(Amount.from_json('0')).to_text_full());
    });
    it('Multiply 0 USD with 0 XRP', function() {
      assert.strictEqual('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('0')).to_text_full());
    });
    it('Multiply 0 XRP with 0 USD', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').multiply(Amount.from_json('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply 1 XRP with 0 XRP', function() {
      assert.strictEqual('0/XRP', Amount.from_json('1').multiply(Amount.from_json('0')).to_text_full());
    });
    it('Multiply 1 USD with 0 XRP', function() {
      assert.strictEqual('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('0')).to_text_full());
    });
    it('Multiply 1 XRP with 0 USD', function() {
      assert.strictEqual('0/XRP', Amount.from_json('1').multiply(Amount.from_json('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply 0 XRP with 1 XRP', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').multiply(Amount.from_json('1')).to_text_full());
    });
    it('Multiply 0 USD with 1 XRP', function() {
      assert.strictEqual('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('1')).to_text_full());
    });
    it('Multiply 0 XRP with 1 USD', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').multiply(Amount.from_json('1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply XRP with USD', function() {
      assert.equal('0.002/XRP', Amount.from_json('200').multiply(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply XRP with USD', function() {
      assert.strictEqual('0.2/XRP', Amount.from_json('20000').multiply(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply XRP with USD', function() {
      assert.strictEqual('20/XRP', Amount.from_json('2000000').multiply(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply XRP with USD, neg', function() {
      assert.strictEqual('-0.002/XRP', Amount.from_json('200').multiply(Amount.from_json('-10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply XRP with USD, neg, frac', function() {
      assert.strictEqual('-0.222/XRP', Amount.from_json('-6000').multiply(Amount.from_json('37/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply USD with USD', function() {
      assert.strictEqual('20000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply USD with USD', function() {
      assert.strictEqual('200000000000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('2000000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('100000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply EUR with USD, result < 1', function() {
      assert.strictEqual('100000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('1000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply EUR with USD, neg', function() {
      assert.strictEqual('-48000000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-24000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply EUR with USD, neg, <1', function() {
      assert.strictEqual('-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('0.1/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('-1000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply EUR with XRP, factor < 1', function() {
      assert.strictEqual('100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('0.05/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('2000')).to_text_full());
    });
    it('Multiply EUR with XRP, neg', function() {
      assert.strictEqual('-500/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('5')).to_text_full());
    });
    it('Multiply EUR with XRP, neg, <1', function() {
      assert.strictEqual('-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-0.05/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('2000')).to_text_full());
    });
    it('Multiply XRP with XRP', function() {
      assert.strictEqual('0.0001/XRP', Amount.from_json('10').multiply(Amount.from_json('10')).to_text_full());
    });
    it('Divide XRP by USD', function() {
      assert.strictEqual('0.00002/XRP', Amount.from_json('200').divide(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Divide XRP by USD', function() {
      assert.strictEqual('0.002/XRP', Amount.from_json('20000').divide(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Divide XRP by USD', function() {
      assert.strictEqual('0.2/XRP', Amount.from_json('2000000').divide(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Divide XRP by USD, neg', function() {
      assert.strictEqual('-0.00002/XRP', Amount.from_json('200').divide(Amount.from_json('-10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Divide XRP by USD, neg, frac', function() {
      assert.strictEqual('-0.000162/XRP', Amount.from_json('-6000').divide(Amount.from_json('37/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Divide USD by USD', function() {
      assert.strictEqual('200/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Divide USD by USD, fractional', function() {
      assert.strictEqual('57142.85714285714/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('2000000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('35/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Divide USD by USD', function() {
      assert.strictEqual('20/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('2000000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('100000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Divide EUR by USD, factor < 1', function() {
      assert.strictEqual('0.1/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('1000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Divide EUR by USD, neg', function() {
      assert.strictEqual('-12/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-24000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Divide EUR by USD, neg, <1', function() {
      assert.strictEqual('-0.1/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('-1000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Divide EUR by XRP, result < 1', function() {
      assert.strictEqual('0.05/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('2000')).to_text_full());
    });
    it('Divide EUR by XRP, neg', function() {
      assert.strictEqual('-20/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('5')).to_text_full());
    });
    it('Divide EUR by XRP, neg, <1', function() {
      assert.strictEqual('-0.05/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('2000')).to_text_full());
    });
    it('Negate native 123 human', function() {
      assert.strictEqual('-0.000123/XRP', Amount.from_json('123').negate().to_human_full());
    });
    it('Negate native -123 human', function() {
      assert.strictEqual('0.000123/XRP', Amount.from_json('-123').negate().to_human_full());
    });
    it('Negate non-native 123 human', function() {
      assert.strictEqual('-123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').negate().to_human_full());
    });
    it('Negate non-native -123 human', function() {
      assert.strictEqual('123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').negate().to_human_full());
    });
    it('Clone non-native -123 human', function() {
      assert.strictEqual('-123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-123/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').clone().to_human_full());
    });
    it('Add XRP to XRP human', function() {
      assert.strictEqual('0.0002/XRP', Amount.from_json('150').add(Amount.from_json('50')).to_human_full());
    });
    it('Add USD to USD human', function() {
      assert.strictEqual('200.52/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('150.02/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').add(Amount.from_json('50.5/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Add 0 USD to 1 USD human', function() {
      assert.strictEqual('1', Amount.from_json('1/USD').add('0/USD').to_human());
    });
    it('Subtract USD from USD human', function() {
      assert.strictEqual('99.52/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('150.02/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').subtract(Amount.from_json('50.5/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply 0 XRP with 0 XRP human', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').multiply(Amount.from_json('0')).to_human_full());
    });
    it('Multiply 0 USD with 0 XRP human', function() {
      assert.strictEqual('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('0')).to_human_full());
    });
    it('Multiply 0 XRP with 0 USD human', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').multiply(Amount.from_json('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply 1 XRP with 0 XRP human', function() {
      assert.strictEqual('0/XRP', Amount.from_json('1').multiply(Amount.from_json('0')).to_human_full());
    });
    it('Multiply 1 USD with 0 XRP human', function() {
      assert.strictEqual('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('0')).to_human_full());
    });
    it('Multiply 1 XRP with 0 USD human', function() {
      assert.strictEqual('0/XRP', Amount.from_json('1').multiply(Amount.from_json('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply 0 XRP with 1 XRP human', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').multiply(Amount.from_json('1')).to_human_full());
    });
    it('Multiply 0 USD with 1 XRP human', function() {
      assert.strictEqual('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('1')).to_human_full());
    });
    it('Multiply 0 XRP with 1 USD human', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').multiply(Amount.from_json('1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply XRP with USD human', function() {
      assert.equal('0.002/XRP', Amount.from_json('200').multiply(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply XRP with USD human', function() {
      assert.strictEqual('0.2/XRP', Amount.from_json('20000').multiply(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply XRP with USD human', function() {
      assert.strictEqual('20/XRP', Amount.from_json('2000000').multiply(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply XRP with USD, neg human', function() {
      assert.strictEqual('-0.002/XRP', Amount.from_json('200').multiply(Amount.from_json('-10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply XRP with USD, neg, frac human', function() {
      assert.strictEqual('-0.222/XRP', Amount.from_json('-6000').multiply(Amount.from_json('37/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply USD with USD human', function() {
      assert.strictEqual('20,000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply USD with USD human', function() {
      assert.strictEqual('200,000,000,000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('2000000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('100000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply EUR with USD, result < 1 human', function() {
      assert.strictEqual('100,000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('1000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply EUR with USD, neg human', function() {
      assert.strictEqual('-48,000,000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-24000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply EUR with USD, neg, <1 human', function() {
      assert.strictEqual('-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('0.1/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('-1000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply EUR with XRP, factor < 1 human', function() {
      assert.strictEqual('100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('0.05/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('2000')).to_human_full());
    });
    it('Multiply EUR with XRP, neg human', function() {
      assert.strictEqual('-500/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('5')).to_human_full());
    });
    it('Multiply EUR with XRP, neg, <1 human', function() {
      assert.strictEqual('-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-0.05/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').multiply(Amount.from_json('2000')).to_human_full());
    });
    it('Multiply XRP with XRP human', function() {
      assert.strictEqual('0.0001/XRP', Amount.from_json('10').multiply(Amount.from_json('10')).to_human_full());
    });
    it('Divide XRP by USD human', function() {
      assert.strictEqual('0.00002/XRP', Amount.from_json('200').divide(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Divide XRP by USD human', function() {
      assert.strictEqual('0.002/XRP', Amount.from_json('20000').divide(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Divide XRP by USD human', function() {
      assert.strictEqual('0.2/XRP', Amount.from_json('2000000').divide(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Divide XRP by USD, neg human', function() {
      assert.strictEqual('-0.00002/XRP', Amount.from_json('200').divide(Amount.from_json('-10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Divide XRP by USD, neg, frac human', function() {
      assert.strictEqual('-0.000162/XRP', Amount.from_json('-6000').divide(Amount.from_json('37/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Divide USD by USD human', function() {
      assert.strictEqual('200/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Divide USD by USD, fractional human', function() {
      assert.strictEqual('57,142.85714285714/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('2000000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('35/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Divide USD by USD human', function() {
      assert.strictEqual('20/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('2000000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('100000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Divide EUR by USD, factor < 1 human', function() {
      assert.strictEqual('0.1/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('1000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Divide EUR by USD, neg human', function() {
      assert.strictEqual('-12/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-24000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Divide EUR by USD, neg, <1 human', function() {
      assert.strictEqual('-0.1/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('-1000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Divide EUR by XRP, result < 1 human', function() {
      assert.strictEqual('0.05/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('2000')).to_human_full());
    });
    it('Divide EUR by XRP, neg human', function() {
      assert.strictEqual('-20/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('5')).to_human_full());
    });
    it('Divide EUR by XRP, neg, <1 human', function() {
      assert.strictEqual('-0.05/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').divide(Amount.from_json('2000')).to_human_full());
    });
    it('Divide by zero should throw', function() {
      assert.throws(function() {
        Amount.from_json(1).divide(Amount.from_json(0));
      });
    });
    it('Divide zero by number', function() {
      assert.strictEqual('0', Amount.from_json(0).divide(Amount.from_json(1)).to_text());
    });
    it('Divide invalid by number', function() {
      assert.throws(function() {
        Amount.from_json('x').divide(Amount.from_json('1'));
      });
    });
    it('Divide number by invalid', function() {
      assert.throws(function() {
        Amount.from_json('1').divide(Amount.from_json('x'));
      });
    });
    it('amount.abs -1 == 1', function() {
      assert.strictEqual('1', Amount.from_json(-1).abs().to_text());
    });
    it('amount.copyTo native', function() {
      assert(isNaN(Amount.from_json('x').copyTo(new Amount())._value));
    });
    it('amount.copyTo zero', function() {
      assert(!(Amount.from_json(0).copyTo(new Amount())._is_negative));
    });
  });
  describe('Amount comparisons', function() {
    it('0 USD == 0 USD amount.equals string argument', function() {
      const a = '0/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL';
      assert(Amount.from_json(a).equals(a));
    });
    it('0 USD == 0 USD', function() {
      const a = Amount.from_json('0/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      const b = Amount.from_json('0/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      assert(a.equals(b));
      assert(!a.not_equals_why(b));
    });
    it('0 USD == -0 USD', function() {
      const a = Amount.from_json('0/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      const b = Amount.from_json('-0/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      assert(a.equals(b));
      assert(!a.not_equals_why(b));
    });
    it('0 XRP == 0 XRP', function() {
      const a = Amount.from_json('0');
      const b = Amount.from_json('0');
      assert(a.equals(b));
      assert(!a.not_equals_why(b));
    });
    it('0 XRP == -0 XRP', function() {
      const a = Amount.from_json('0');
      const b = Amount.from_json('-0');
      assert(a.equals(b));
      assert(!a.not_equals_why(b));
    });
    it('10 USD == 10 USD', function() {
      const a = Amount.from_json('10/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      const b = Amount.from_json('10/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      assert(a.equals(b));
      assert(!a.not_equals_why(b));
    });
    it('123.4567 USD == 123.4567 USD', function() {
      const a = Amount.from_json('123.4567/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      const b = Amount.from_json('123.4567/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      assert(a.equals(b));
      assert(!a.not_equals_why(b));
    });
    it('10 XRP == 10 XRP', function() {
      const a = Amount.from_json('10');
      const b = Amount.from_json('10');
      assert(a.equals(b));
      assert(!a.not_equals_why(b));
    });
    it('1.1 XRP == 1.1 XRP', function() {
      const a = Amount.from_json('1100000');
      const b = Amount.from_json('11000000').ratio_human('10/XRP');
      assert(a.equals(b));
      assert(!a.not_equals_why(b));
    });
    it('0 USD == 0 USD (ignore issuer)', function() {
      const a = Amount.from_json('0/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      const b = Amount.from_json('0/USD/rH5aWQJ4R7v4Mpyf4kDBUvDFT5cbpFq3XP');
      assert(a.equals(b, true));
      assert(!a.not_equals_why(b, true));
    });
    it('1.1 USD == 1.10 USD (ignore issuer)', function() {
      const a = Amount.from_json('1.1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      const b = Amount.from_json('1.10/USD/rH5aWQJ4R7v4Mpyf4kDBUvDFT5cbpFq3XP');
      assert(a.equals(b, true));
      assert(!a.not_equals_why(b, true));
    });
    // Exponent mismatch
    it('10 USD != 100 USD', function() {
      const a = Amount.from_json('10/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      const b = Amount.from_json('100/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      assert(!a.equals(b));
      assert.strictEqual(a.not_equals_why(b), 'Non-XRP value differs.');
    });
    it('10 XRP != 100 XRP', function() {
      const a = Amount.from_json('10');
      const b = Amount.from_json('100');
      assert(!a.equals(b));
      assert.strictEqual(a.not_equals_why(b), 'XRP value differs.');
    });
    // Mantissa mismatch
    it('1 USD != 2 USD', function() {
      const a = Amount.from_json('1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      const b = Amount.from_json('2/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      assert(!a.equals(b));
      assert.strictEqual(a.not_equals_why(b), 'Non-XRP value differs.');
    });
    it('1 XRP != 2 XRP', function() {
      const a = Amount.from_json('1');
      const b = Amount.from_json('2');
      assert(!a.equals(b));
      assert.strictEqual(a.not_equals_why(b), 'XRP value differs.');
    });
    it('0.1 USD != 0.2 USD', function() {
      const a = Amount.from_json('0.1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      const b = Amount.from_json('0.2/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      assert(!a.equals(b));
      assert.strictEqual(a.not_equals_why(b), 'Non-XRP value differs.');
    });
    // Sign mismatch
    it('1 USD != -1 USD', function() {
      const a = Amount.from_json('1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      const b = Amount.from_json('-1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      assert(!a.equals(b));
      assert.strictEqual(a.not_equals_why(b), 'Non-XRP sign differs.');
    });
    it('1 XRP != -1 XRP', function() {
      const a = Amount.from_json('1');
      const b = Amount.from_json('-1');
      assert(!a.equals(b));
      assert.strictEqual(a.not_equals_why(b), 'XRP sign differs.');
    });
    it('1 USD != 1 USD (issuer mismatch)', function() {
      const a = Amount.from_json('1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      const b = Amount.from_json('1/USD/rH5aWQJ4R7v4Mpyf4kDBUvDFT5cbpFq3XP');
      assert(!a.equals(b));
      assert.strictEqual(a.not_equals_why(b), 'Non-XRP issuer differs: rH5aWQJ4R7v4Mpyf4kDBUvDFT5cbpFq3XP/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
    });
    it('1 USD != 1 EUR', function() {
      const a = Amount.from_json('1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      const b = Amount.from_json('1/EUR/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      assert(!a.equals(b));
      assert.strictEqual(a.not_equals_why(b), 'Non-XRP currency differs.');
    });
    it('1 USD != 1 XRP', function() {
      const a = Amount.from_json('1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      const b = Amount.from_json('1');
      assert(!a.equals(b));
      assert.strictEqual(a.not_equals_why(b), 'Native mismatch.');
    });
    it('1 XRP != 1 USD', function() {
      const a = Amount.from_json('1');
      const b = Amount.from_json('1/USD/rNDKeo9RrCiRdfsMG8AdoZvNZxHASGzbZL');
      assert(!a.equals(b));
      assert.strictEqual(a.not_equals_why(b), 'Native mismatch.');
    });
  });

  describe('product_human', function() {
    it('Multiply 0 XRP with 0 XRP', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').product_human(Amount.from_json('0')).to_text_full());
    });
    it('Multiply 0 USD with 0 XRP', function() {
      assert.strictEqual('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('0')).to_text_full());
    });
    it('Multiply 0 XRP with 0 USD', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').product_human(Amount.from_json('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply 1 XRP with 0 XRP', function() {
      assert.strictEqual('0/XRP', Amount.from_json('1').product_human(Amount.from_json('0')).to_text_full());
    });
    it('Multiply 1 USD with 0 XRP', function() {
      assert.strictEqual('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('0')).to_text_full());
    });
    it('Multiply 1 XRP with 0 USD', function() {
      assert.strictEqual('0/XRP', Amount.from_json('1').product_human(Amount.from_json('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply 0 XRP with 1 XRP', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').product_human(Amount.from_json('1')).to_text_full());
    });
    it('Multiply 0 USD with 1 XRP', function() {
      assert.strictEqual('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('1')).to_text_full());
    });
    it('Multiply 0 XRP with 1 USD', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').product_human(Amount.from_json('1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply XRP with USD', function() {
      assert.equal('0.002/XRP', Amount.from_json('200').product_human(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply XRP with USD', function() {
      assert.strictEqual('0.2/XRP', Amount.from_json('20000').product_human(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply XRP with USD', function() {
      assert.strictEqual('20/XRP', Amount.from_json('2000000').product_human(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply XRP with USD, neg', function() {
      assert.strictEqual('-0.002/XRP', Amount.from_json('200').product_human(Amount.from_json('-10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply XRP with USD, neg, frac', function() {
      assert.strictEqual('-0.222/XRP', Amount.from_json('-6000').product_human(Amount.from_json('37/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply USD with USD', function() {
      assert.strictEqual('20000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply USD with USD', function() {
      assert.strictEqual('200000000000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('2000000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('100000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply EUR with USD, result < 1', function() {
      assert.strictEqual('100000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('1000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full());
    });
    it('Multiply EUR with USD, neg', function() {
      assert.strictEqual(Amount.from_json('-24000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full(), '-48000000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Multiply EUR with USD, neg, <1', function() {
      assert.strictEqual(Amount.from_json('0.1/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('-1000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_text_full(), '-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Multiply EUR with XRP, factor < 1', function() {
      assert.strictEqual(Amount.from_json('0.05/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('2000')).to_text_full(), '0.0001/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Multiply EUR with XRP, neg', function() {
      assert.strictEqual(Amount.from_json('-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('5')).to_text_full(), '-0.0005/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Multiply EUR with XRP, neg, <1', function() {
      assert.strictEqual(Amount.from_json('-0.05/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('2000')).to_text_full(), '-0.0001/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Multiply XRP with XRP', function() {
      assert.strictEqual(Amount.from_json('10000000').product_human(Amount.from_json('10')).to_text_full(), '0.0001/XRP');
    });
    it('Multiply USD with XAU (dem)', function() {
      assert.strictEqual(Amount.from_json('2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('10/015841551A748AD2C1F76FF6ECB0CCCD00000000/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh'), {reference_date: 443845330 + 31535000}).to_text_full(), '19900.00316303883/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Multiply 0 XRP with 0 XRP human', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').product_human(Amount.from_json('0')).to_human_full());
    });
    it('Multiply 0 USD with 0 XRP human', function() {
      assert.strictEqual('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('0')).to_human_full());
    });
    it('Multiply 0 XRP with 0 USD human', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').product_human(Amount.from_json('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply 1 XRP with 0 XRP human', function() {
      assert.strictEqual('0/XRP', Amount.from_json('1').product_human(Amount.from_json('0')).to_human_full());
    });
    it('Multiply 1 USD with 0 XRP human', function() {
      assert.strictEqual('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('0')).to_human_full());
    });
    it('Multiply 1 XRP with 0 USD human', function() {
      assert.strictEqual('0/XRP', Amount.from_json('1').product_human(Amount.from_json('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply 0 XRP with 1 XRP human', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').product_human(Amount.from_json('1')).to_human_full());
    });
    it('Multiply 0 USD with 1 XRP human', function() {
      assert.strictEqual('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('0/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('1')).to_human_full());
    });
    it('Multiply 0 XRP with 1 USD human', function() {
      assert.strictEqual('0/XRP', Amount.from_json('0').product_human(Amount.from_json('1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply XRP with USD human', function() {
      assert.equal('0.002/XRP', Amount.from_json('200').product_human(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply XRP with USD human', function() {
      assert.strictEqual('0.2/XRP', Amount.from_json('20000').product_human(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply XRP with USD human', function() {
      assert.strictEqual('20/XRP', Amount.from_json('2000000').product_human(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply XRP with USD, neg human', function() {
      assert.strictEqual('-0.002/XRP', Amount.from_json('200').product_human(Amount.from_json('-10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply XRP with USD, neg, frac human', function() {
      assert.strictEqual('-0.222/XRP', Amount.from_json('-6000').product_human(Amount.from_json('37/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply USD with USD human', function() {
      assert.strictEqual('20,000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('10/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply USD with USD human', function() {
      assert.strictEqual('200,000,000,000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('2000000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('100000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply EUR with USD, result < 1 human', function() {
      assert.strictEqual('100,000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', Amount.from_json('100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('1000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full());
    });
    it('Multiply EUR with USD, neg human', function() {
      assert.strictEqual(Amount.from_json('-24000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full(), '-48,000,000/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Multiply EUR with USD, neg, <1 human', function() {
      assert.strictEqual(Amount.from_json('0.1/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('-1000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh')).to_human_full(), '-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Multiply EUR with XRP, factor < 1 human', function() {
      assert.strictEqual(Amount.from_json('0.05/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('2000')).to_human_full(), '0.0001/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Multiply EUR with XRP, neg human', function() {
      assert.strictEqual(Amount.from_json('-100/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('5')).to_human_full(), '-0.0005/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Multiply EUR with XRP, neg, <1 human', function() {
      assert.strictEqual(Amount.from_json('-0.05/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('2000')).to_human_full(), '-0.0001/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Multiply XRP with XRP human', function() {
      assert.strictEqual(Amount.from_json('10000000').product_human(Amount.from_json('10')).to_human_full(), '0.0001/XRP');
    });
    it('Multiply USD with XAU (dem) human', function() {
      assert.strictEqual(Amount.from_json('2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').product_human(Amount.from_json('10/015841551A748AD2C1F76FF6ECB0CCCD00000000/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh'), {reference_date: 443845330 + 31535000}).to_human_full(), '19,900.00316303883/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
  });

  describe('ratio_human', function() {
    it('Divide USD by XAU (dem)', function() {
      assert.strictEqual(Amount.from_json('2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').ratio_human(Amount.from_json('10/015841551A748AD2C1F76FF6ECB0CCCD00000000/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh'), {reference_date: 443845330 + 31535000}).to_text_full(), '201.0049931765529/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Divide USD by XAU (dem) human', function() {
      assert.strictEqual(Amount.from_json('2000/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').ratio_human(Amount.from_json('10/015841551A748AD2C1F76FF6ECB0CCCD00000000/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh'), {reference_date: 443845330 + 31535000}).to_human_full(), '201.0049931765529/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
  });

  describe('_invert', function() {
    it('Invert 1', function() {
      assert.strictEqual(Amount.from_json('1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').invert().to_text_full(), '1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Invert 20', function() {
      assert.strictEqual(Amount.from_json('20/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').invert().to_text_full(), '0.05/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Invert 0.02', function() {
      assert.strictEqual(Amount.from_json('0.02/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').invert().to_text_full(), '50/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Invert 1 human', function() {
      assert.strictEqual(Amount.from_json('1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').invert().to_human_full(), '1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Invert 20 human', function() {
      assert.strictEqual(Amount.from_json('20/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').invert().to_human_full(), '0.05/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Invert 0.02 human', function() {
      assert.strictEqual(Amount.from_json('0.02/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh').invert().to_human_full(), '50/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
  });

  describe('from_quality', function() {
    it('XRP/XRP', function() {
      assert.throws(function() {
        Amount.from_quality('7B73A610A009249B0CC0D4311E8BA7927B5A34D86634581C5F0FF9FF678E1000', 'XRP', NaN, {base_currency: 'XRP'}).to_text_full();
      });
    });
    it('BTC/XRP', function() {
      assert.strictEqual(Amount.from_quality('7B73A610A009249B0CC0D4311E8BA7927B5A34D86634581C5F0FF9FF678E1000', 'XRP', NaN, {base_currency: 'BTC'}).to_text_full(), '44,970/XRP');
    });
    it('BTC/XRP inverse', function() {
      assert.strictEqual(Amount.from_quality('37AAC93D336021AE94310D0430FFA090F7137C97D473488C4A0918D0DEF8624E', 'XRP', NaN, {inverse: true, base_currency: 'BTC'}).to_text_full(), '39,053.954453/XRP');
    });
    it('XRP/USD', function() {
      assert.strictEqual(Amount.from_quality('DFA3B6DDAB58C7E8E5D944E736DA4B7046C30E4F460FD9DE4D05DCAA8FE12000', 'USD', 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B', {base_currency: 'XRP'}).to_text_full(), '0.0165/USD/rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B');
    });
    it('XRP/USD inverse', function() {
      assert.strictEqual(Amount.from_quality('4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C22A840E27DCA9B', 'USD', 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B', {inverse: true, base_currency: 'XRP'}).to_text_full(), '0.010251/USD/rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B');
    });
    it('BTC/USD', function() {
      assert.strictEqual(Amount.from_quality('6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC9858038D7EA4C68000', 'USD', 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B', {base_currency: 'BTC'}).to_text_full(), '1000/USD/rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B');
    });
    it('BTC/USD inverse', function() {
      assert.strictEqual(Amount.from_quality('20294C923E80A51B487EB9547B3835FD483748B170D2D0A455071AFD498D0000', 'USD', 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B', {inverse: true, base_currency: 'BTC'}).to_text_full(), '0.5/USD/rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B');
    });
    it('XAU(dem)/XRP', function() {
      assert.strictEqual(Amount.from_quality('587322CCBDE0ABD01704769A73A077C32FB39057D813D4165F1FF973CAF997EF', 'XRP', NaN, {base_currency: '015841551A748AD2C1F76FF6ECB0CCCD00000000', reference_date: 443845330 + 31535000}).to_text_full(), '90,452.246928/XRP');
    });
    it('XAU(dem)/XRP inverse', function() {
      assert.strictEqual(Amount.from_quality('F72C7A9EAE4A45ED1FB547AD037D07B9B965C6E662BEBAFA4A03F2A976804235', 'XRP', NaN, {inverse: true, base_currency: '015841551A748AD2C1F76FF6ECB0CCCD00000000', reference_date: 443845330 + 31535000}).to_text_full(), '90,442.196677/XRP');
    });
    it('USD/XAU(dem)', function() {
      assert.strictEqual(Amount.from_quality('4743E58E44974B325D42FD2BB683A6E36950F350EE46DD3A521B644B99782F5F', '015841551A748AD2C1F76FF6ECB0CCCD00000000', 'rUyPiNcSFFj6uMR2gEaD8jUerQ59G1qvwN', {base_currency: 'USD', reference_date: 443845330 + 31535000}).to_text_full(), '0.007710100231303007/XAU (-0.5%pa)/rUyPiNcSFFj6uMR2gEaD8jUerQ59G1qvwN');
    });
    it('USD/XAU(dem) inverse', function() {
      assert.strictEqual(Amount.from_quality('CDFD3AFB2F8C5DBEF75B081F7C957FF5509563266F28F36C5704A0FB0BAD8800', '015841551A748AD2C1F76FF6ECB0CCCD00000000', 'rUyPiNcSFFj6uMR2gEaD8jUerQ59G1qvwN', {inverse: true, base_currency: 'USD', reference_date: 443845330 + 31535000}).to_text_full(), '0.007675186123263489/XAU (-0.5%pa)/rUyPiNcSFFj6uMR2gEaD8jUerQ59G1qvwN');
    });
    it('BTC/XRP human', function() {
      assert.strictEqual(Amount.from_quality('7B73A610A009249B0CC0D4311E8BA7927B5A34D86634581C5F0FF9FF678E1000', 'XRP', NaN, {base_currency: 'BTC'}).to_human_full(), '44,970/XRP');
    });
    it('BTC/XRP inverse human', function() {
      assert.strictEqual(Amount.from_quality('37AAC93D336021AE94310D0430FFA090F7137C97D473488C4A0918D0DEF8624E', 'XRP', NaN, {inverse: true, base_currency: 'BTC'}).to_human_full(), '39,053.954453/XRP');
    });
    it('XRP/USD human', function() {
      assert.strictEqual(Amount.from_quality('DFA3B6DDAB58C7E8E5D944E736DA4B7046C30E4F460FD9DE4D05DCAA8FE12000', 'USD', 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B', {base_currency: 'XRP'}).to_human_full(), '0.0165/USD/rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B');
    });
    it('XRP/USD inverse human', function() {
      assert.strictEqual(Amount.from_quality('4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C22A840E27DCA9B', 'USD', 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B', {inverse: true, base_currency: 'XRP'}).to_human_full(), '0.010251/USD/rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B');
    });
    it('BTC/USD human', function() {
      assert.strictEqual(Amount.from_quality('6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC9858038D7EA4C68000', 'USD', 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B', {base_currency: 'BTC'}).to_human_full(), '1,000/USD/rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B');
    });
    it('BTC/USD inverse human', function() {
      assert.strictEqual(Amount.from_quality('20294C923E80A51B487EB9547B3835FD483748B170D2D0A455071AFD498D0000', 'USD', 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B', {inverse: true, base_currency: 'BTC'}).to_human_full(), '0.5/USD/rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B');
    });
    it('XAU(dem)/XRP human', function() {
      assert.strictEqual(Amount.from_quality('587322CCBDE0ABD01704769A73A077C32FB39057D813D4165F1FF973CAF997EF', 'XRP', NaN, {base_currency: '015841551A748AD2C1F76FF6ECB0CCCD00000000', reference_date: 443845330 + 31535000}).to_human_full(), '90,452.246928/XRP');
    });
    it('XAU(dem)/XRP inverse human', function() {
      assert.strictEqual(Amount.from_quality('F72C7A9EAE4A45ED1FB547AD037D07B9B965C6E662BEBAFA4A03F2A976804235', 'XRP', NaN, {inverse: true, base_currency: '015841551A748AD2C1F76FF6ECB0CCCD00000000', reference_date: 443845330 + 31535000}).to_human_full(), '90,442.196677/XRP');
    });
    it('USD/XAU(dem) human', function() {
      assert.strictEqual(Amount.from_quality('4743E58E44974B325D42FD2BB683A6E36950F350EE46DD3A521B644B99782F5F', '015841551A748AD2C1F76FF6ECB0CCCD00000000', 'rUyPiNcSFFj6uMR2gEaD8jUerQ59G1qvwN', {base_currency: 'USD', reference_date: 443845330 + 31535000}).to_human_full(), '0.007710100231303007/XAU (-0.5%pa)/rUyPiNcSFFj6uMR2gEaD8jUerQ59G1qvwN');
    });
    it('USD/XAU(dem) inverse human', function() {
      assert.strictEqual(Amount.from_quality('CDFD3AFB2F8C5DBEF75B081F7C957FF5509563266F28F36C5704A0FB0BAD8800', '015841551A748AD2C1F76FF6ECB0CCCD00000000', 'rUyPiNcSFFj6uMR2gEaD8jUerQ59G1qvwN', {inverse: true, base_currency: 'USD', reference_date: 443845330 + 31535000}).to_human_full(), '0.007675186123263489/XAU (-0.5%pa)/rUyPiNcSFFj6uMR2gEaD8jUerQ59G1qvwN');
    });
  });

  describe('apply interest', function() {
    it('from_json apply interest 10 XAU', function() {
      let demAmount = Amount.from_json('10/0158415500000000C1F76FF6ECB0BAC600000000/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      assert.strictEqual(demAmount.to_text_full(), '10/XAU (-0.5%pa)/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      demAmount = demAmount.applyInterest(459990264);
      assert.strictEqual(demAmount.to_text_full(), '9.294949401870436/XAU (-0.5%pa)/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');

    });
    it('from_json apply interest XAU', function() {
      let demAmount = Amount.from_json('1235.5/0158415500000000C1F76FF6ECB0BAC600000000/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      assert.strictEqual(demAmount.to_text_full(), '1235.5/XAU (-0.5%pa)/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      demAmount = demAmount.applyInterest(459990264);
      assert.strictEqual(demAmount.to_text_full(), '1148.390998601092/XAU (-0.5%pa)/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('from_human with reference date', function() {
      const demAmount = Amount.from_human('10 0158415500000000C1F76FF6ECB0BAC600000000', {reference_date: 459990264});
      demAmount.set_issuer('rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      assert.strictEqual(demAmount.to_text_full(), '10.75853086191915/XAU (-0.5%pa)/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('from_json apply interest 10 XAU human', function() {
      let demAmount = Amount.from_json('10/0158415500000000C1F76FF6ECB0BAC600000000/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      assert.strictEqual(demAmount.to_human_full(), '10/XAU (-0.5%pa)/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      demAmount = demAmount.applyInterest(459990264);
      assert.strictEqual(demAmount.to_human_full(), '9.294949401870436/XAU (-0.5%pa)/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');

    });
    it('from_json apply interest XAU human', function() {
      let demAmount = Amount.from_json('1235.5/0158415500000000C1F76FF6ECB0BAC600000000/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      assert.strictEqual(demAmount.to_human_full(), '1,235.5/XAU (-0.5%pa)/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      demAmount = demAmount.applyInterest(459990264);
      assert.strictEqual(demAmount.to_human_full(), '1,148.390998601092/XAU (-0.5%pa)/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('from_human with reference date human', function() {
      const demAmount = Amount.from_human('10 0158415500000000C1F76FF6ECB0BAC600000000', {reference_date: 459990264});
      demAmount.set_issuer('rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      assert.strictEqual(demAmount.to_human_full(), '10.75853086191915/XAU (-0.5%pa)/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
  });

  describe('amount limits', function() {
    it('max JSON wire limite', function() {
      assert.strictEqual(Amount.bi_xns_max, '100000000000000000');
    });

    it('max JSON wire limite', function() {
      assert.strictEqual(Amount.bi_xns_min, '-100000000000000000');
    });

    it('max mantissa value', function() {
      assert.strictEqual(Amount.bi_man_max_value, '9999999999999999');
    });

    it('min mantissa value', function() {
      assert.strictEqual(Amount.bi_man_min_value, '1000000000000000');
    });

    it('from_json minimum XRP', function() {
      const amt = Amount.from_json('-100000000000000000');
      assert.strictEqual(amt.to_json(), '-100000000000000000');
    });

    it('from_json maximum XRP', function() {
      const amt = Amount.from_json('100000000000000000');
      assert.strictEqual(amt.to_json(), '100000000000000000');
    });

    it('from_json less than minimum XRP', function() {
      assert.throws(function() {
        Amount.from_json('-100000000000000001');
      });
    });

    it('from_json more than maximum XRP', function() {
      assert.throws(function() {
        Amount.from_json('100000000000000001');
      });
    });

    it('from_json minimum IOU', function() {
      const amt = Amount.from_json('-1e-81/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      assert.strictEqual(amt.to_text(), '-1000000000000000e-96');
      assert.strictEqual(amt.to_text(), Amount.min_value);
    });

    it('from_json exceed minimum IOU', function() {
      assert.throws(function() {
        Amount.from_json('-1e-82/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      }, 'Exceeding min value of ' + Amount.min_value);
    });

    it('from_json maximum IOU', function() {
      const amt = Amount.from_json('9999999999999999e80/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      assert.strictEqual(amt.to_text(), '9999999999999999e80');
    });

    it('from_json exceed maximum IOU', function() {
      assert.throws(function() {
        Amount.from_json('9999999999999999e81/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      }, 'Exceeding max value of ' + Amount.max_value);
    });

    it('from_json normalize mantissa to valid max range, lost significant digits', function() {
      const amt = Amount.from_json('99999999999999999999999999999999/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      assert.strictEqual(amt.to_text(), '9999999999999999e16');
    });

    it('from_json normalize mantissa to min valid range, lost significant digits', function() {
      const amt = Amount.from_json('-0.0000000000000000000000001/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      assert.strictEqual(amt.to_text(), '-1000000000000000e-40');
    });
  });
});
