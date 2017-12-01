/* eslint-disable max-len */

'use strict';

const assert = require('assert');
const BN = require('bn.js');
const BigNumber = require('bignumber.js');
const SerializedObject = require('ripple-lib').SerializedObject;
const types = require('ripple-lib').types;
const Amount = require('ripple-lib').Amount;

describe('Serialized types', function() {
  describe('Int8', function() {
    it('Serialize 0', function() {
      const so = new SerializedObject();
      types.Int8.serialize(so, 0);
      assert.strictEqual(so.to_hex(), '00');
    });
    it('Serialize 123', function() {
      const so = new SerializedObject();
      types.Int8.serialize(so, 123);
      assert.strictEqual(so.to_hex(), '7B');
    });
    it('Serialize 255', function() {
      const so = new SerializedObject();
      types.Int8.serialize(so, 255);
      assert.strictEqual(so.to_hex(), 'FF');
    });
    it('Fail to serialize 256', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int8.serialize(so, 256);
      });
    });
    it('Fail to serialize -1', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int8.serialize(so, -1);
      });
    });
    it('Serialize 5.5 (should floor)', function() {
      const so = new SerializedObject();
      types.Int8.serialize(so, 5.5);
      assert.strictEqual(so.to_hex(), '05');
    });
    it('Serialize 255.9 (should floor)', function() {
      const so = new SerializedObject();
      types.Int8.serialize(so, 255.9);
      assert.strictEqual(so.to_hex(), 'FF');
    });
    it('Fail to serialize null', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int8.serialize(so, null);
      });
    });
    it('Fail to serialize "bla"', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int8.serialize(so, 'bla');
      });
    });
    it('Fail to serialize {}', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int8.serialize(so, {});
      });
    });
  });

  describe('Int16', function() {
    it('Serialize 0', function() {
      const so = new SerializedObject();
      types.Int16.serialize(so, 0);
      assert.strictEqual(so.to_hex(), '0000');
    });
    it('Serialize 123', function() {
      const so = new SerializedObject();
      types.Int16.serialize(so, 123);
      assert.strictEqual(so.to_hex(), '007B');
    });
    it('Serialize 255', function() {
      const so = new SerializedObject();
      types.Int16.serialize(so, 255);
      assert.strictEqual(so.to_hex(), '00FF');
    });
    it('Serialize 256', function() {
      const so = new SerializedObject();
      types.Int16.serialize(so, 256);
      assert.strictEqual(so.to_hex(), '0100');
    });
    it('Serialize 65535', function() {
      const so = new SerializedObject();
      types.Int16.serialize(so, 65535);
      assert.strictEqual(so.to_hex(), 'FFFF');
    });
    it('Fail to serialize 65536', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int8.serialize(so, 65536);
      });
    });
    it('Fail to serialize -1', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int16.serialize(so, -1);
      });
    });
    it('Serialize 123.5 (should floor)', function() {
      const so = new SerializedObject();
      types.Int16.serialize(so, 123.5);
      assert.strictEqual(so.to_hex(), '007B');
    });
    it('Serialize 65535.5 (should floor)', function() {
      const so = new SerializedObject();
      types.Int16.serialize(so, 65535.5);
      assert.strictEqual(so.to_hex(), 'FFFF');
    });
    it('Fail to serialize null', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int16.serialize(so, null);
      });
    });
    it('Fail to serialize "bla"', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int16.serialize(so, 'bla');
      });
    });
    it('Fail to serialize {}', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int16.serialize(so, {});
      });
    });
  });

  describe('Int32', function() {
    it('Serialize 0', function() {
      const so = new SerializedObject();
      types.Int32.serialize(so, 0);
      assert.strictEqual(so.to_hex(), '00000000');
    });
    it('Serialize 123', function() {
      const so = new SerializedObject();
      types.Int32.serialize(so, 123);
      assert.strictEqual(so.to_hex(), '0000007B');
    });
    it('Serialize 255', function() {
      const so = new SerializedObject();
      types.Int32.serialize(so, 255);
      assert.strictEqual(so.to_hex(), '000000FF');
    });
    it('Serialize 256', function() {
      const so = new SerializedObject();
      types.Int32.serialize(so, 256);
      assert.strictEqual(so.to_hex(), '00000100');
    });
    it('Serialize 0xF0F0F0F0', function() {
      const so = new SerializedObject();
      types.Int32.serialize(so, 0xF0F0F0F0);
      assert.strictEqual(so.to_hex(), 'F0F0F0F0');
    });
    it('Serialize 0xFFFFFFFF', function() {
      const so = new SerializedObject();
      types.Int32.serialize(so, 0xFFFFFFFF);
      assert.strictEqual(so.to_hex(), 'FFFFFFFF');
    });
    it('Fail to serialize 0x100000000', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int8.serialize(so, 0x100000000);
      });
    });
    it('Fail to serialize -1', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int32.serialize(so, -1);
      });
    });
    it('Serialize 123.5 (should floor)', function() {
      const so = new SerializedObject();
      types.Int32.serialize(so, 123.5);
      assert.strictEqual(so.to_hex(), '0000007B');
    });
    it('Serialize 4294967295.5 (should floor)', function() {
      const so = new SerializedObject();
      types.Int32.serialize(so, 4294967295.5);
      assert.strictEqual(so.to_hex(), 'FFFFFFFF');
    });
    it('Fail to serialize null', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int32.serialize(so, null);
      });
    });
    it('Fail to serialize "bla"', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int32.serialize(so, 'bla');
      });
    });
    it('Fail to serialize {}', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int32.serialize(so, {});
      });
    });
    it('Parse 0', function() {
      const val = '00000000';
      const so = new SerializedObject(val);
      const num = types.Int32.parse(so);
      assert.strictEqual(num, parseInt(val, 16));
    });
    it('Parse 1', function() {
      const val = '00000001';
      const so = new SerializedObject(val);
      const num = types.Int32.parse(so);
      assert.strictEqual(num, parseInt(val, 16));
    });
    it('Parse UINT32_MAX', function() {
      const val = 'FFFFFFFF';
      const so = new SerializedObject(val);
      const num = types.Int32.parse(so);
      assert.strictEqual(num, parseInt(val, 16));
    });
  });

  describe('Int64', function() {
    it('Serialize 0', function() {
      const so = new SerializedObject();
      types.Int64.serialize(so, 0);
      assert.strictEqual(so.to_hex(), '0000000000000000');
    });
    it('Serialize 123', function() {
      const so = new SerializedObject();
      types.Int64.serialize(so, 123);
      assert.strictEqual(so.to_hex(), '000000000000007B');
    });
    it('Serialize 255', function() {
      const so = new SerializedObject();
      types.Int64.serialize(so, 255);
      assert.strictEqual(so.to_hex(), '00000000000000FF');
    });
    it('Serialize 256', function() {
      const so = new SerializedObject();
      types.Int64.serialize(so, 256);
      assert.strictEqual(so.to_hex(), '0000000000000100');
    });
    it('Serialize 0xF0F0F0F0', function() {
      const so = new SerializedObject();
      types.Int64.serialize(so, 0xF0F0F0F0);
      assert.strictEqual(so.to_hex(), '00000000F0F0F0F0');
    });
    it('Serialize 0xFFFFFFFF', function() {
      const so = new SerializedObject();
      types.Int64.serialize(so, 0xFFFFFFFF);
      assert.strictEqual(so.to_hex(), '00000000FFFFFFFF');
    });
    it('Serialize 0x100000000', function() {
      const so = new SerializedObject();
      types.Int64.serialize(so, 0x100000000);
      assert.strictEqual(so.to_hex(), '0000000100000000');
    });
    it('Fail to serialize 0x100000000', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int8.serialize(so, 0x100000000);
      });
    });
    it('Fail to serialize -1', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int64.serialize(so, -1);
      });
    });
    it('Serialize 123.5 (should floor)', function() {
      const so = new SerializedObject();
      types.Int64.serialize(so, 123.5);
      assert.strictEqual(so.to_hex(), '000000000000007B');
    });
    it('Serialize 4294967295.5 (should floor)', function() {
      const so = new SerializedObject();
      types.Int64.serialize(so, 4294967295.5);
      assert.strictEqual(so.to_hex(), '00000000FFFFFFFF');
    });
    it('Does not get confused when the high bit is set', function() {
      const so = new SerializedObject();
      types.Int64.serialize(so, '8B2386F26F8E232B');
      assert.strictEqual(so.to_hex(), '8B2386F26F8E232B');
      const so2 = new SerializedObject('8B2386F26F8E232B');
      const num = types.Int64.parse(so2);
      // We get a positive number
      assert.strictEqual(num.toString(16), '8b2386f26f8e232b');
    });
    it('Serialize "0123456789ABCDEF"', function() {
      const so = new SerializedObject();
      types.Int64.serialize(so, '0123456789ABCDEF');
      assert.strictEqual(so.to_hex(), '0123456789ABCDEF');
    });
    it('Serialize "F0E1D2C3B4A59687"', function() {
      const so = new SerializedObject();
      types.Int64.serialize(so, 'F0E1D2C3B4A59687');
      assert.strictEqual(so.to_hex(), 'F0E1D2C3B4A59687');
    });
    it('Serialize bn("FFEEDDCCBBAA9988")', function() {
      const so = new SerializedObject();
      types.Int64.serialize(so, new BN('FFEEDDCCBBAA9988', 16));
      assert.strictEqual(so.to_hex(), 'FFEEDDCCBBAA9988');
    });
    it('Fail to serialize BigNumber("-1")', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int64.serialize(so, new BigNumber('-1', 10));
      });
    });
    it('Fail to serialize "10000000000000000"', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int64.serialize(so, '10000000000000000');
      });
    });
    it('Fail to serialize "110000000000000000"', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int64.serialize(so, '110000000000000000');
      });
    });
    it('Fail to serialize null', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int64.serialize(so, null);
      });
    });
    it('Fail to serialize "bla"', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int64.serialize(so, 'bla');
      });
    });
    it('Fail to serialize {}', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        types.Int64.serialize(so, {});
      });
    });
    it('Parse "0123456789ABCDEF"', function() {
      const so = new SerializedObject('0123456789ABCDEF');
      const num = types.Int64.parse(so);
      assert.strictEqual(num.toString(16), '123456789abcdef');
    });
  });

  describe('Hash128', function() {
    it('Serialize 0', function() {
      const so = new SerializedObject();
      types.Hash128.serialize(so, '00000000000000000000000000000000');
      assert.strictEqual(so.to_hex(), '00000000000000000000000000000000');
    });
    it('Serialize 102030405060708090A0B0C0D0E0F000', function() {
      const so = new SerializedObject();
      types.Hash128.serialize(so, '102030405060708090A0B0C0D0E0F000');
      assert.strictEqual(so.to_hex(), '102030405060708090A0B0C0D0E0F000');
    });
    it('Serialize HASH128_MAX', function() {
      const so = new SerializedObject();
      types.Hash128.serialize(so, 'FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF');
      assert.strictEqual(so.to_hex(), 'FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF');
    });
    it('Parse 0', function() {
      const val = '00000000000000000000000000000000';
      const so = new SerializedObject(val);
      const num = types.Hash128.parse(so);
      assert.strictEqual(num.to_hex(), val);
    });
    it('Parse 1', function() {
      const val = '00000000000000000000000000000001';
      const so = new SerializedObject(val);
      const num = types.Hash128.parse(so);
      assert.strictEqual(num.to_hex(), val);
    });
    it('Parse HASH128_MAX', function() {
      const val = 'FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF';
      const so = new SerializedObject(val);
      const num = types.Hash128.parse(so);
      assert.strictEqual(num.to_hex(), val);
    });
  });

  describe('Hash160', function() {
    it('Serialize 0', function() {
      const hex = '0000000000000000000000000000000000000000';
      const base58 = 'rrrrrrrrrrrrrrrrrrrrrhoLvTp';
      const so = new SerializedObject();
      types.Hash160.serialize(so, base58);
      assert.strictEqual(so.to_hex(), hex);

      const so2 = new SerializedObject();
      types.Hash160.serialize(so2, hex);
      assert.strictEqual(so2.to_hex(), hex);
    });
    it('Serialize 1', function() {
      const hex = '0000000000000000000000000000000000000001';
      const base58 = 'rrrrrrrrrrrrrrrrrrrrBZbvji';
      const so = new SerializedObject();
      types.Hash160.serialize(so, base58);
      assert.strictEqual(so.to_hex(), hex);

      const so2 = new SerializedObject();
      types.Hash160.serialize(so2, hex);
      assert.strictEqual(so2.to_hex(), hex);
    });
    it('Serialize FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF', function() {
      const hex = 'FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF';
      const base58 = 'rQLbzfJH5BT1FS9apRLKV3G8dWEA5njaQi';
      const so = new SerializedObject();
      types.Hash160.serialize(so, base58);
      assert.strictEqual(so.to_hex(), hex);

      const so2 = new SerializedObject();
      types.Hash160.serialize(so2, hex);
      assert.strictEqual(so2.to_hex(), hex);
    });
    it('Parse 0', function() {
      const val = '0000000000000000000000000000000000000000';
      const so = new SerializedObject(val);
      const num = types.Hash160.parse(so);
      assert.strictEqual(num.to_hex(), val);
    });
    it('Parse 1', function() {
      const val = '0000000000000000000000000000000000000001';
      const so = new SerializedObject(val);
      const num = types.Hash160.parse(so);
      assert.strictEqual(num.to_hex(), val);
    });
    it('Parse HASH160_MAX', function() {
      const val = 'FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF';
      const so = new SerializedObject(val);
      const num = types.Hash160.parse(so);
      assert.strictEqual(num.to_hex(), val);
    });
    it('Parse 0 as JSON', function() {
      // Hash160 should be returned as hex in JSON, unlike
      // addresses.
      const val = '0000000000000000000000000000000000000000';
      const so = new SerializedObject(val);
      const num = types.Hash160.parse(so);
      assert.strictEqual(num.to_json(), val);
    });
  });

  describe('Hash256', function() {
    it('Serialize 0', function() {
      const so = new SerializedObject();
      types.Hash256.serialize(so, '0000000000000000000000000000000000000000000000000000000000000000');
      assert.strictEqual(so.to_hex(), '0000000000000000000000000000000000000000000000000000000000000000');
    });
    it('Serialize 1', function() {
      const so = new SerializedObject();
      types.Hash256.serialize(so, '0000000000000000000000000000000000000000000000000000000000000001');
      assert.strictEqual(so.to_hex(), '0000000000000000000000000000000000000000000000000000000000000001');
    });
    it('Serialize HASH256_MAX', function() {
      const so = new SerializedObject();
      types.Hash256.serialize(so, 'FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF');
      assert.strictEqual(so.to_hex(), 'FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF');
    });
    it('Parse 0', function() {
      const val = '0000000000000000000000000000000000000000000000000000000000000000';
      const so = new SerializedObject(val);
      const num = types.Hash256.parse(so);
      assert.strictEqual(num.to_hex(), val);
    });
    it('Parse 1', function() {
      const val = '0000000000000000000000000000000000000000000000000000000000000000';
      const so = new SerializedObject(val);
      const num = types.Hash256.parse(so);
      assert.strictEqual(num.to_hex(), val);
    });
    it('Parse HASH256_MAX', function() {
      const val = 'FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF';
      const so = new SerializedObject(val);
      const num = types.Hash256.parse(so);
      assert.strictEqual(num.to_hex(), val);
    });
  });

  describe('Quality', function() {
    it('Serialize 1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      const so = new SerializedObject();
      types.Quality.serialize(so, '1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      assert.strictEqual(so.to_hex(), '55038D7EA4C68000');
    });
    it('Serialize 87654321.12345678/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      const so = new SerializedObject();
      types.Quality.serialize(so, '87654321.12345678/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      assert.strictEqual(so.to_hex(), '5C1F241D335BF24E');
    });
  });

  describe('Amount', function() {
    it('Serialize 0 XRP', function() {
      const so = new SerializedObject();
      types.Amount.serialize(so, '0');
      assert.strictEqual(so.to_hex(), '4000000000000000');
    });
    it('Serialize 1 XRP', function() {
      const so = new SerializedObject();
      types.Amount.serialize(so, '1');
      assert.strictEqual(so.to_hex(), '4000000000000001');
    });
    it('Serialize -1 XRP', function() {
      const so = new SerializedObject();
      types.Amount.serialize(so, '-1');
      assert.strictEqual(so.to_hex(), '0000000000000001');
    });
    it('Serialize 213 XRP', function() {
      const so = new SerializedObject();
      types.Amount.serialize(so, '213');
      assert.strictEqual(so.to_hex(), '40000000000000D5');
    });
    it('Serialize 270544960 XRP', function() {
      const so = new SerializedObject();
      types.Amount.serialize(so, '270544960');
      assert.strictEqual(so.to_hex(), '4000000010203040');
    });
    it('Serialize 1161981756646125568 XRP', function() {
      const so = new SerializedObject();
      assert.throws(function() {
        const amt = Amount.from_json('1161981756646125696');
        types.Amount.serialize(so, amt);
      });
    });
    it('Serialize 1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      const so = new SerializedObject();
      types.Amount.serialize(so, '1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      assert.strictEqual(so.to_hex(), 'D4838D7EA4C680000000000000000000000000005553440000000000B5F762798A53D543A014CAF8B297CFF8F2F937E8');
    });
    it('Serialize 87654321.12345678/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      const so = new SerializedObject();
      types.Amount.serialize(so, '87654321.12345678/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      assert.strictEqual(so.to_hex(), 'D65F241D335BF24E0000000000000000000000004555520000000000B5F762798A53D543A014CAF8B297CFF8F2F937E8');
    });
    it('Serialize -1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      const so = new SerializedObject();
      types.Amount.serialize(so, '-1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      assert.strictEqual(so.to_hex(), '94838D7EA4C680000000000000000000000000005553440000000000B5F762798A53D543A014CAF8B297CFF8F2F937E8');
    });
    it('Serialize 15/XRP/rM1oqKtfh1zgjdAgbFmaRm3btfGBX25xVo', function() {
      // This actually appears in the ledger, so we need to be able to serialize
      // Transaction #A2AD66C93C7B7277CD5AEB718A4E82D88C7099129948BC66A394EE38B34657A9
      const so = new SerializedObject();
      types.Amount.serialize(so, {
        value: '1000',
        currency: 'XRP',
        issuer: 'rM1oqKtfh1zgjdAgbFmaRm3btfGBX25xVo'
      });
      assert.strictEqual(so.to_hex(), 'D5438D7EA4C680000000000000000000000000005852500000000000E4FE687C90257D3D2D694C8531CDEECBE84F3367');
    });
    // Test support for 20-byte hex raw currency codes
    it('Serialize 15/015841551A748AD23FEFFFFFFFEA028000000000/1', function() {
      const so = new SerializedObject();
      types.Amount.serialize(so, {
        value: '1000',
        currency: '015841551A748AD23FEFFFFFFFEA028000000000',
        issuer: 'rM1oqKtfh1zgjdAgbFmaRm3btfGBX25xVo'
      });
      assert.strictEqual(so.to_hex(), 'D5438D7EA4C68000015841551A748AD23FEFFFFFFFEA028000000000E4FE687C90257D3D2D694C8531CDEECBE84F3367');
    });
    it('Serialize max_value/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      const so = new SerializedObject();
      types.Amount.serialize(so, Amount.max_value + '/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
      assert.strictEqual(so.to_hex(), 'EC6386F26FC0FFFF0000000000000000000000005553440000000000B5F762798A53D543A014CAF8B297CFF8F2F937E8');
    });
    it('Parse 1 XRP', function() {
      const so = new SerializedObject('4000000000000001');
      assert.strictEqual(types.Amount.parse(so).to_json(), '1');
    });
    it('Parse -1 XRP', function() {
      const so = new SerializedObject('0000000000000001');
      assert.strictEqual(types.Amount.parse(so).to_json(), '-1');
    });
    it('Parse 213 XRP', function() {
      const so = new SerializedObject('40000000000000D5');
      assert.strictEqual(types.Amount.parse(so).to_json(), '213');
    });
    it('Parse 270544960 XRP', function() {
      const so = new SerializedObject('4000000010203040');
      assert.strictEqual(types.Amount.parse(so).to_json(), '270544960');
    });
    it('Parse 1161981756646125568 XRP', function() {
      assert.throws(function() {
        // hex(1161981756646125568) = 1020304050607000
        const so = new SerializedObject('1020304050607000');
        types.Amount.parse(so).to_json();
      });
    });
    it('Parse 1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      const so = new SerializedObject('D4838D7EA4C680000000000000000000000000005553440000000000B5F762798A53D543A014CAF8B297CFF8F2F937E8');
      assert.strictEqual(types.Amount.parse(so).to_text_full(), '1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Parse 87654321.12345678/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      const so = new SerializedObject('D65F241D335BF24E0000000000000000000000004555520000000000B5F762798A53D543A014CAF8B297CFF8F2F937E8');
      assert.strictEqual(types.Amount.parse(so).to_text_full(), '87654321.12345678/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Parse -1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      const so = new SerializedObject('94838D7EA4C680000000000000000000000000005553440000000000B5F762798A53D543A014CAF8B297CFF8F2F937E8');
      assert.strictEqual(types.Amount.parse(so).to_text_full(), '-1/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
    it('Parse max_value/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh', function() {
      const so = new SerializedObject('EC6386F26FC0FFFF0000000000000000000000005553440000000000B5F762798A53D543A014CAF8B297CFF8F2F937E8');
      assert.strictEqual(types.Amount.parse(so).to_text_full(), Amount.max_value + '/USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh');
    });
  });

  describe('Account', function() {
    it('Serialize 0', function() {
      const hex = '0000000000000000000000000000000000000000';
      const base58 = 'rrrrrrrrrrrrrrrrrrrrrhoLvTp';
      const so = new SerializedObject();
      types.Account.serialize(so, base58);
      assert.strictEqual(so.to_hex(), '14' + hex);

      const so2 = new SerializedObject();
      types.Account.serialize(so2, hex);
      assert.strictEqual(so2.to_hex(), '14' + hex);
    });
    it('Serialize 1', function() {
      const hex = '0000000000000000000000000000000000000001';
      const base58 = 'rrrrrrrrrrrrrrrrrrrrBZbvji';
      const so = new SerializedObject();
      types.Account.serialize(so, base58);
      assert.strictEqual(so.to_hex(), '14' + hex);

      const so2 = new SerializedObject();
      types.Account.serialize(so2, hex);
      assert.strictEqual(so2.to_hex(), '14' + hex);
    });
    it('Serialize FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF', function() {
      const hex = 'FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF';
      const base58 = 'rQLbzfJH5BT1FS9apRLKV3G8dWEA5njaQi';
      const so = new SerializedObject();
      types.Account.serialize(so, base58);
      assert.strictEqual(so.to_hex(), '14' + hex);

      const so2 = new SerializedObject();
      types.Account.serialize(so2, hex);
      assert.strictEqual(so2.to_hex(), '14' + hex);
    });
    it('Parse 0', function() {
      const val = '140000000000000000000000000000000000000000';
      const so = new SerializedObject(val);
      const num = types.Account.parse(so);
      assert.strictEqual(num.to_json(), 'rrrrrrrrrrrrrrrrrrrrrhoLvTp');
    });
    it('Parse 1', function() {
      const val = '140000000000000000000000000000000000000001';
      const so = new SerializedObject(val);
      const num = types.Account.parse(so);
      assert.strictEqual(num.to_json(), 'rrrrrrrrrrrrrrrrrrrrBZbvji');
    });
    it('Parse HASH160_MAX', function() {
      const val = '14FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF';
      const so = new SerializedObject(val);
      const num = types.Account.parse(so);
      assert.strictEqual(num.to_json(), 'rQLbzfJH5BT1FS9apRLKV3G8dWEA5njaQi');
    });
  });

  describe('PathSet', function() {
    it('Serialize single empty path [[]]', function() {
      const so = new SerializedObject();
      types.PathSet.serialize(so, [[]]);
      assert.strictEqual(so.to_hex(), '00');
    });
    it('Serialize [[e],[e,e]]', function() {
      const so = new SerializedObject();
      types.PathSet.serialize(so, [[{
        account: 123,
        currency: 'USD',
        issuer: 789
      }],
      [{
        account: 123,
        currency: 'BTC',
        issuer: 789
      },
      {
        account: 987,
        currency: 'EUR',
        issuer: 321
      }]]);
      assert.strictEqual(so.to_hex(), '31000000000000000000000000000000000000007B00000000000000000000000055534400000000000000000000000000000000000000000000000315FF31000000000000000000000000000000000000007B000000000000000000000000425443000000000000000000000000000000000000000000000003153100000000000000000000000000000000000003DB0000000000000000000000004555520000000000000000000000000000000000000000000000014100'); // TODO: Check this independently
    });
    it('Serialize path through XRP', function() {
      const hex = '31000000000000000000000000000000000000007B00000000000000000000000055534400000000000000000000000000000000000000000000000315FF1000000000000000000000000000000000000000003100000000000000000000000000000000000003DB0000000000000000000000004555520000000000000000000000000000000000000000000000014100';
      const json = [
        [{
          account: 'rrrrrrrrrrrrrrrrrrrrNxV3Xza',
          currency: 'USD',
          issuer: 'rrrrrrrrrrrrrrrrrrrpYnYCNYf'
        }],
        [{
          currency: 'XRP'
        }, {
          account: 'rrrrrrrrrrrrrrrrrrrpvQsW3V3',
          currency: 'EUR',
          issuer: 'rrrrrrrrrrrrrrrrrrrdHRtqg2'
        }]
      ];

      const result_json = [
        [{
          account: 'rrrrrrrrrrrrrrrrrrrrNxV3Xza',
          currency: 'USD',
          issuer: 'rrrrrrrrrrrrrrrrrrrpYnYCNYf',
          type: 49,
          type_hex: '0000000000000031'
        }],
        [{
          currency: 'XRP',
          type: 16,
          type_hex: '0000000000000010'
        }, {
          account: 'rrrrrrrrrrrrrrrrrrrpvQsW3V3',
          currency: 'EUR',
          issuer: 'rrrrrrrrrrrrrrrrrrrdHRtqg2',
          type: 49,
          type_hex: '0000000000000031'
        }]
      ];

      const so = new SerializedObject();
      types.PathSet.serialize(so, json);
      assert.strictEqual(so.to_hex(), hex);

      const so2 = new SerializedObject(hex);
      const parsed_path = SerializedObject.jsonify_structure(types.PathSet.parse(so2));
      assert.deepEqual(parsed_path, result_json);
    });
    it('Serialize path through XRP IOUs', function() {
      const hex = '31000000000000000000000000000000000000007B00000000000000000000000055534400000000000000000000000000000000000000000000000315FF1000000000000000000000000058525000000000003100000000000000000000000000000000000003DB0000000000000000000000004555520000000000000000000000000000000000000000000000014100';
      const json = [
        [{
          account: 'rrrrrrrrrrrrrrrrrrrrNxV3Xza',
          currency: 'USD',
          issuer: 'rrrrrrrrrrrrrrrrrrrpYnYCNYf'
        }],
        [{
          currency: 'XRP',
          non_native: true
        }, {
          account: 'rrrrrrrrrrrrrrrrrrrpvQsW3V3',
          currency: 'EUR',
          issuer: 'rrrrrrrrrrrrrrrrrrrdHRtqg2'
        }]
      ];

      const result_json = [
        [{
          account: 'rrrrrrrrrrrrrrrrrrrrNxV3Xza',
          currency: 'USD',
          issuer: 'rrrrrrrrrrrrrrrrrrrpYnYCNYf',
          type: 49,
          type_hex: '0000000000000031'
        }],
        [{
          currency: 'XRP',
          non_native: true,
          type: 16,
          type_hex: '0000000000000010'
        }, {
          account: 'rrrrrrrrrrrrrrrrrrrpvQsW3V3',
          currency: 'EUR',
          issuer: 'rrrrrrrrrrrrrrrrrrrdHRtqg2',
          type: 49,
          type_hex: '0000000000000031'
        }]
      ];

      const so = new SerializedObject();
      types.PathSet.serialize(so, json);
      assert.strictEqual(so.to_hex(), hex);

      const so2 = new SerializedObject(hex);
      const parsed_path = SerializedObject.jsonify_structure(types.PathSet.parse(so2));
      assert.deepEqual(parsed_path, result_json);
    });
    it('Serialize path through XRP IOUs (realistic example)', function() {
      // Appears in the history
      // TX #0CBB429C456ED999CC691DFCC8E62E8C8C7E9522C2BEA967FED0D7E2A9B28D13
      // Note that XRP IOUs are no longer allowed, so this functionality is
      // for historic transactions only.

      const hex = '31585E1F3BD02A15D6185F8BB9B57CC60DEDDB37C10000000000000000000000004254430000000000585E1F3BD02A15D6185F8BB9B57CC60DEDDB37C131E4FE687C90257D3D2D694C8531CDEECBE84F33670000000000000000000000004254430000000000E4FE687C90257D3D2D694C8531CDEECBE84F3367310A20B3C85F482532A9578DBB3950B85CA06594D100000000000000000000000042544300000000000A20B3C85F482532A9578DBB3950B85CA06594D13000000000000000000000000055534400000000000A20B3C85F482532A9578DBB3950B85CA06594D1FF31585E1F3BD02A15D6185F8BB9B57CC60DEDDB37C10000000000000000000000004254430000000000585E1F3BD02A15D6185F8BB9B57CC60DEDDB37C131E4FE687C90257D3D2D694C8531CDEECBE84F33670000000000000000000000004254430000000000E4FE687C90257D3D2D694C8531CDEECBE84F33673115036E2D3F5437A83E5AC3CAEE34FF2C21DEB618000000000000000000000000425443000000000015036E2D3F5437A83E5AC3CAEE34FF2C21DEB6183000000000000000000000000055534400000000000A20B3C85F482532A9578DBB3950B85CA06594D1FF31585E1F3BD02A15D6185F8BB9B57CC60DEDDB37C10000000000000000000000004254430000000000585E1F3BD02A15D6185F8BB9B57CC60DEDDB37C13157180C769B66D942EE69E6DCC940CA48D82337AD000000000000000000000000425443000000000057180C769B66D942EE69E6DCC940CA48D82337AD1000000000000000000000000058525000000000003000000000000000000000000055534400000000000A20B3C85F482532A9578DBB3950B85CA06594D100';
      const json = [
        [{
          account: 'r9hEDb4xBGRfBCcX3E4FirDWQBAYtpxC8K',
          currency: 'BTC',
          issuer: 'r9hEDb4xBGRfBCcX3E4FirDWQBAYtpxC8K'
        }, {
          account: 'rM1oqKtfh1zgjdAgbFmaRm3btfGBX25xVo',
          currency: 'BTC',
          issuer: 'rM1oqKtfh1zgjdAgbFmaRm3btfGBX25xVo'
        }, {
          account: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
          currency: 'BTC',
          issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
        }, {
          currency: 'USD',
          issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
        }],
        [{
          account: 'r9hEDb4xBGRfBCcX3E4FirDWQBAYtpxC8K',
          currency: 'BTC',
          issuer: 'r9hEDb4xBGRfBCcX3E4FirDWQBAYtpxC8K'
        }, {
          account: 'rM1oqKtfh1zgjdAgbFmaRm3btfGBX25xVo',
          currency: 'BTC',
          issuer: 'rM1oqKtfh1zgjdAgbFmaRm3btfGBX25xVo'
        }, {
          account: 'rpvfJ4mR6QQAeogpXEKnuyGBx8mYCSnYZi',
          currency: 'BTC',
          issuer: 'rpvfJ4mR6QQAeogpXEKnuyGBx8mYCSnYZi'
        }, {
          currency: 'USD',
          issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
        }],
        [{
          account: 'r9hEDb4xBGRfBCcX3E4FirDWQBAYtpxC8K',
          currency: 'BTC',
          issuer: 'r9hEDb4xBGRfBCcX3E4FirDWQBAYtpxC8K'
        }, {
          account: 'r3AWbdp2jQLXLywJypdoNwVSvr81xs3uhn',
          currency: 'BTC',
          issuer: 'r3AWbdp2jQLXLywJypdoNwVSvr81xs3uhn'
        }, {
          currency: 'XRP',
          non_native: true
        }, {
          currency: 'USD',
          issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
        }]
      ];

      const result_json = [
        [{
          account: 'r9hEDb4xBGRfBCcX3E4FirDWQBAYtpxC8K',
          currency: 'BTC',
          issuer: 'r9hEDb4xBGRfBCcX3E4FirDWQBAYtpxC8K',
          type: 49,
          type_hex: '0000000000000031'
        }, {
          account: 'rM1oqKtfh1zgjdAgbFmaRm3btfGBX25xVo',
          currency: 'BTC',
          issuer: 'rM1oqKtfh1zgjdAgbFmaRm3btfGBX25xVo',
          type: 49,
          type_hex: '0000000000000031'
        }, {
          account: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
          currency: 'BTC',
          issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
          type: 49,
          type_hex: '0000000000000031'
        }, {
          currency: 'USD',
          issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
          type: 48,
          type_hex: '0000000000000030'
        }],
        [{
          account: 'r9hEDb4xBGRfBCcX3E4FirDWQBAYtpxC8K',
          currency: 'BTC',
          issuer: 'r9hEDb4xBGRfBCcX3E4FirDWQBAYtpxC8K',
          type: 49,
          type_hex: '0000000000000031'
        }, {
          account: 'rM1oqKtfh1zgjdAgbFmaRm3btfGBX25xVo',
          currency: 'BTC',
          issuer: 'rM1oqKtfh1zgjdAgbFmaRm3btfGBX25xVo',
          type: 49,
          type_hex: '0000000000000031'
        }, {
          account: 'rpvfJ4mR6QQAeogpXEKnuyGBx8mYCSnYZi',
          currency: 'BTC',
          issuer: 'rpvfJ4mR6QQAeogpXEKnuyGBx8mYCSnYZi',
          type: 49,
          type_hex: '0000000000000031'
        }, {
          currency: 'USD',
          issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
          type: 48,
          type_hex: '0000000000000030'
        }],
        [{
          account: 'r9hEDb4xBGRfBCcX3E4FirDWQBAYtpxC8K',
          currency: 'BTC',
          issuer: 'r9hEDb4xBGRfBCcX3E4FirDWQBAYtpxC8K',
          type: 49,
          type_hex: '0000000000000031'
        }, {
          account: 'r3AWbdp2jQLXLywJypdoNwVSvr81xs3uhn',
          currency: 'BTC',
          issuer: 'r3AWbdp2jQLXLywJypdoNwVSvr81xs3uhn',
          type: 49,
          type_hex: '0000000000000031'
        }, {
          currency: 'XRP',
          non_native: true,
          type: 16,
          type_hex: '0000000000000010'
        }, {
          currency: 'USD',
          issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
          type: 48,
          type_hex: '0000000000000030'
        }]
      ];

      const so = new SerializedObject();
      types.PathSet.serialize(so, json);
      assert.strictEqual(so.to_hex(), hex);

      const so2 = new SerializedObject(hex);
      const parsed_path = SerializedObject.jsonify_structure(types.PathSet.parse(so2));
      assert.deepEqual(parsed_path, result_json);
    });
    it('Parse single empty path [[]]', function() {
      const so = new SerializedObject('00');
      const parsed_path = SerializedObject.jsonify_structure(types.PathSet.parse(so));
      assert.deepEqual(parsed_path, [[]]);
    });
    it('Parse [[e],[e,e]]', function() {
      const so = new SerializedObject('31000000000000000000000000000000000000007B00000000000000000000000055534400000000000000000000000000000000000000000000000315FF31000000000000000000000000000000000000007B000000000000000000000000425443000000000000000000000000000000000000000000000003153100000000000000000000000000000000000003DB0000000000000000000000004555520000000000000000000000000000000000000000000000014100');

      const parsed_path = types.PathSet.parse(so);
      const comp = [
        [
          {
            account: 'rrrrrrrrrrrrrrrrrrrrNxV3Xza',
            currency: 'USD',
            issuer: 'rrrrrrrrrrrrrrrrrrrpYnYCNYf',
            type: 49,
            type_hex: '0000000000000031'
          }
        ],
        [
          {
            account: 'rrrrrrrrrrrrrrrrrrrrNxV3Xza',
            currency: 'BTC',
            issuer: 'rrrrrrrrrrrrrrrrrrrpYnYCNYf',
            type: 49,
            type_hex: '0000000000000031'
          },
          {
            account: 'rrrrrrrrrrrrrrrrrrrpvQsW3V3',
            currency: 'EUR',
            issuer: 'rrrrrrrrrrrrrrrrrrrdHRtqg2',
            type: 49,
            type_hex: '0000000000000031'
          }
        ]
      ];

      assert.deepEqual(SerializedObject.jsonify_structure(parsed_path, ''), comp);
    });
  });

  describe('Object', function() {
    it('Can parse objects with VL encoded Vector256', function() {
      const hex = '110064220000000058000360186E008422E06B72D5B275E29EE3BE9D87A370F424E0E7BF613C4659098214289D19799C892637306AAAF03805EDFCDF6C28B8011320081342A0AB45459A54D8E4FA1842339A102680216CF9A152BCE4F4CE467D8246';
      const so = new SerializedObject(hex);
      const as_json = so.to_json();
      const expected_json = {
        LedgerEntryType: 'DirectoryNode',
        Owner: 'rh6kN9s7spSb3vdv6H8ZGYzsddSLeEUGmc',
        Flags: 0,
        Indexes: [
          '081342A0AB45459A54D8E4FA1842339A102680216CF9A152BCE4F4CE467D8246'
        ],
        RootIndex: '000360186E008422E06B72D5B275E29EE3BE9D87A370F424E0E7BF613C465909'
      };
      assert.deepEqual(as_json, expected_json);
      assert.strictEqual(SerializedObject.from_json(expected_json).to_hex(), hex);
    });
    it('Serialize empty object {}', function() {
      const so = new SerializedObject();
      types.Object.serialize(so, {});
      assert.strictEqual(so.to_hex(), 'E1');
    });
    it('Parse empty object {}', function() {
      const so = new SerializedObject('E1');
      const parsed_object = types.Object.parse(so);
      assert.deepEqual(parsed_object, {});
    });
    it('Serialize simple object {TakerPays:"87654321.12345678/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", TakerGets:213, Fee:"789"}', function() {
      const so = new SerializedObject();
      types.Object.serialize(so, {
        TakerPays: '87654321.12345678/EUR/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh',
        TakerGets: '213',
        Fee: 789
      });
      assert.strictEqual(so.to_hex(), '64D65F241D335BF24E0000000000000000000000004555520000000000B5F762798A53D543A014CAF8B297CFF8F2F937E86540000000000000D5684000000000000315E1');
      // TODO: Check independently.
    });
    it('Parse same object', function() {
      const so = new SerializedObject('64D65F241D335BF24E0000000000000000000000004555520000000000B5F762798A53D543A014CAF8B297CFF8F2F937E86540000000000000D5684000000000000315E1');
      const parsed_object = types.Object.parse(so);
      const comp = {
        TakerPays: {
          value: '87654321.12345678',
          currency: 'EUR',
          issuer: 'rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh'
        },
        TakerGets: '213',
        Fee: '789'
      };
      assert.deepEqual(SerializedObject.jsonify_structure(parsed_object, ''), comp);
      // TODO: Check independently.
    });

    it('Serialize simple object {DestinationTag:123, QualityIn:456, QualityOut:789}', function() {
      const so = new SerializedObject();
      types.Object.serialize(so, {
        DestinationTag: 123,
        QualityIn: 456,
        QualityOut: 789
      });
      assert.strictEqual(so.to_hex(), '2E0000007B2014000001C8201500000315E1');
      // TODO: Check independently.
    });
    it('Parse simple object {DestinationTag:123, QualityIn:456, QualityOut:789}', function() {// 2E0000007B22000001C82400000315E1 2E0000007B2002000001C8200200000315E1
      const so = new SerializedObject('2E0000007B2014000001C8201500000315E1');
      const parsed_object = types.Object.parse(so);
      assert.deepEqual(parsed_object, {
        DestinationTag: 123,
        QualityIn: 456,
        QualityOut: 789
      });
      // TODO: Check independently.
    });
  });

  describe('Array', function() {
    it('Serialize empty array []', function() {
      const so = new SerializedObject();
      types.Array.serialize(so, []);
      assert.strictEqual(so.to_hex(), 'F1');
    });
    it('Parse empty array []', function() {
      const so = new SerializedObject('F1');
      const parsed_object = types.Array.parse(so);
      assert.deepEqual(parsed_object, []);
    });
    it('Serialize 3-length array [{TakerPays:123}); {TakerGets:456}, {Fee:789}]', function() {
      const so = new SerializedObject();
      types.Array.serialize(so, [
        {
          TakerPays: 123
        },
        {
          TakerGets: 456
        },
        {
          Fee: 789
        }
      ]);
      // TODO: Check this manually
      assert.strictEqual(so.to_hex(), '64400000000000007B6540000000000001C8684000000000000315F1');
    });
    it('Parse the same array', function() {
      const so = new SerializedObject('64400000000000007B6540000000000001C8684000000000000315F1');
      const parsed_object = types.Array.parse(so);
      const comp = [
        {
          TakerPays: '123'
        },
        {
          TakerGets: '456'
        },
        {
          Fee: '789'
        }
      ];
      assert.deepEqual(SerializedObject.jsonify_structure(parsed_object, ''), comp);
    });
    it('Serialize 3-length array [{DestinationTag:123}); {QualityIn:456}, {Fee:789}]', function() {
      const so = new SerializedObject();
      types.Array.serialize(so, [
        {
          DestinationTag: 123
        },
        {
          QualityIn: 456
        },
        {
          Fee: 789
        }
      ]);
      // TODO: Check this manually
      assert.strictEqual(so.to_hex(), '2E0000007B2014000001C8684000000000000315F1');
    });
    it('Parse the same array 2', function() {
      const so = new SerializedObject('2E0000007B2014000001C8684000000000000315F1');
      const parsed_object = types.Array.parse(so);
      const comp = [
        {
          DestinationTag: 123
        },
        {
          QualityIn: 456
        },
        {
          Fee: '789'
        }
      ];
      // TODO: Is this correct? Return some things as integers, and others as objects?
      assert.deepEqual(SerializedObject.jsonify_structure(parsed_object, ''), comp);
    });
  });
});

// vim:sw=2:sts=2:ts=8:et
