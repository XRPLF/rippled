'use strict';

/* eslint-disable max-len*/

const assert = require('assert');
const lodash = require('lodash');
const SerializedObject = require('ripple-lib').SerializedObject;
const Amount = require('ripple-lib').Amount;
const sjclcodec = require('sjcl-codec');

// Shortcuts
const hex = sjclcodec.hex;
const utf8 = sjclcodec.utf8String;

describe('Serialized object', function() {

  function convertStringToHex(string) {
    return hex.fromBits(utf8.toBits(string)).toUpperCase();
  }

  describe('#from_json(v).to_json() == v', function() {
    it('outputs same as passed to from_json', function() {
      const input_json = {
        Account: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
        Amount: '274579388',
        Destination: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
        Fee: '15',
        Flags: 0,
        Paths: [[
          {
            account: 'r3kmLJN5D28dHuH8vZNUZpMC43pEHpaocV',
            currency: 'USD',
            issuer: 'r3kmLJN5D28dHuH8vZNUZpMC43pEHpaocV',
            type: 49,
            type_hex: '0000000000000031'
          },
          {
            currency: 'XRP',
            type: 16,
            type_hex: '0000000000000010'
          }
        ]],
        SendMax: {
          currency: 'USD',
          issuer: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
          value: '2.74579388'
        },
        Sequence: 351,
        SigningPubKey: '02854B06CE8F3E65323F89260E9E19B33DA3E01B30EA4CA172612DE77973FAC58A',
        TransactionType: 'Payment',
        TxnSignature: '30450221009DA3A42DD25E3B22EC45AD8BA8FC7A954264264A816D300B2DF69F814D7D4DD2022072C9627F97EEC6DA13DE841E06E2CD985EF06A0FBB15DDBF0800D0730C8986BF'
      };
      const output_json = SerializedObject.from_json(input_json).to_json();
      assert.deepEqual(input_json, output_json);
    });
  });

  describe('#from_json(v).to_json() == v -- invalid amount', function() {
    it('outputs same as passed to from_json', function() {
      const input_json = {
        Account: 'rUR9gTCcrUY9fMkz9rwcM9urPREh3LKXoW',
        Fee: '10',
        Flags: 0,
        Sequence: 65,
        SigningPubKey: '033D0B1FB932E0408C119107483190B61561DCE8529E29CB5D1C69128DA54DA715',
        TakerGets: '2188313981504612096',
        TakerPays: {
          currency: 'USD',
          issuer: 'r9rp9MUFRJVCVLRm3MTmUvSPNBSL3BuEFx',
          value: '99999999999'
        },
        TransactionType: 'OfferCreate',
        TxnSignature: '304602210085C6AE945643150E6D450CF796E45D74FB24B4E03E964A29CC6AFFEB346C77C80221009BE1B6678CF6C2E61F8F2696144C75AFAF66DF4FC0733DF9118EDEFEEFE33243'
      };

      assert.throws(function() {
        SerializedObject.from_json(input_json).to_json();
      });
    });
  });

  describe('#from_json(v).to_json() == v -- invalid amount, strict_mode = false', function() {
    it('outputs same as passed to from_json', function() {
      const input_json = {
        Account: 'rUR9gTCcrUY9fMkz9rwcM9urPREh3LKXoW',
        Fee: '10',
        Flags: 0,
        Sequence: 65,
        SigningPubKey: '033D0B1FB932E0408C119107483190B61561DCE8529E29CB5D1C69128DA54DA715',
        TakerGets: '2188313981504612096',
        TakerPays: {
          currency: 'USD',
          issuer: 'r9rp9MUFRJVCVLRm3MTmUvSPNBSL3BuEFx',
          value: '99999999999'
        },
        TransactionType: 'OfferCreate',
        TxnSignature: 'FFFFFF210085C6AE945643150E6D450CF796E45D74FB24B4E03E964A29CC6AFFEB346C77C80221009BE1B6678CF6C2E61F8F2696144C75AFAF66DF4FC0733DF9118EDEFEEFE33243'
      };

      const strictMode = Amount.strict_mode;
      Amount.strict_mode = false;
      const output_json = SerializedObject.from_json(input_json).to_json();
      assert.deepEqual(input_json, output_json);
      Amount.strict_mode = strictMode;
    });
  });

  describe('#from_json', function() {
    it('understands TransactionType as a Number', function() {
      const input_json = {
        // no non required fields
        Account: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
        Amount: '274579388',
        Destination: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
        Fee: '15',
        Sequence: 351,
        SigningPubKey: '02',// VL field ;)
        TransactionType: 0 //
      };
      const output_json = SerializedObject.from_json(input_json).to_json();

      assert.equal(0, input_json.TransactionType);
      assert.equal('Payment', output_json.TransactionType);
    });

    it('understands LedgerEntryType as a Number', function() {
      const input_json = {
        // no, non required fields
        LedgerEntryType: 100,
        Flags: 0,
        Indexes: [],
        RootIndex: '000360186E008422E06B72D5B275E29EE3BE9D87A370F424E0E7BF613C465909'
      };

      const output_json = SerializedObject.from_json(input_json).to_json();
      assert.equal(100, input_json.LedgerEntryType);
      assert.equal('DirectoryNode', output_json.LedgerEntryType);
    });

    it('checks for missing required fields', function() {
      const input_json = {
        TransactionType: 'Payment',
        // no non required fields
        Account: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
        Amount: '274579388',
        Destination: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
        Fee: '15',
        Sequence: 351,
        SigningPubKey: '02'
      };

      Object.keys(input_json).slice(1).forEach(function(k) {
        const bad_json = lodash.merge({}, input_json);
        delete bad_json[k];

        assert.strictEqual(bad_json[k], undefined);
        assert.throws(function() {
          SerializedObject.from_json(bad_json);
        }, new RegExp('Payment is missing fields: \\["' + k + '"\\]'));

      });
    });
    it('checks for unknown fields', function() {
      const input_json = {
        TransactionType: 'Payment',
        // no non required fields
        Account: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
        Amount: '274579388',
        Destination: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
        Fee: '15',
        Sequence: 351,
        SigningPubKey: '02'
      };

      Object.keys(input_json).slice(1).forEach(function(k) {
        const bad_json = lodash.merge({}, input_json);
        bad_json[k + 'z'] = bad_json[k];

        assert.throws(function() {
          SerializedObject.from_json(bad_json);
        }, new RegExp('Payment has unknown fields: \\["' + k + 'z"\\]'));

      });
    });

    describe('Format validation', function() {
      // Peercover actually had a problem submitting transactions without a `Fee`
      // and rippled was only informing of 'transaction is invalid'
      it('should throw an Error when there is a missing field', function() {
        const input_json = {
          Account: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
          Amount: '274579388',
          Destination: 'r4qLSAzv4LZ9TLsR7diphGwKnSEAMQTSjS',
          Sequence: 351,
          SigningPubKey: '02854B06CE8F3E65323F89260E9E19B33DA3E01B30EA4CA172612DE77973FAC58A',
          TransactionType: 'Payment',
          TxnSignature: '30450221009DA3A42DD25E3B22EC45AD8BA8FC7A954264264A816D300B2DF69F814D7D4DD2022072C9627F97EEC6DA13DE841E06E2CD985EF06A0FBB15DDBF0800D0730C8986BF'
        };
        assert.throws(
          function() {
            SerializedObject.from_json(input_json);
          },
          /Payment is missing fields: \["Fee"\]/
        );
      });
    });

    describe('Memos', function() {
      let input_json;

      beforeEach(function() {
        input_json = {
          'Flags': 2147483648,
          'TransactionType': 'Payment',
          'Account': 'rhXzSyt1q9J8uiFXpK3qSugAAPJKXLtnrF',
          'Amount': '1',
          'Destination': 'radqi6ppXFxVhJdjzaATRBxdrPcVTf1Ung',
          'Sequence': 281,
          'SigningPubKey': '03D642E6457B8AB4D140E2C66EB4C484FAFB1BF267CB578EC4815FE6CD06379C51',
          'Fee': '12000',
          'LastLedgerSequence': 10074214,
          'TxnSignature': '304402201180636F2CE215CE97A29CD302618FAE60D63EBFC8903DE17A356E857A449C430220290F4A54F9DE4AC79034C8BEA5F1F8757F7505F1A6FF04D2E19B6D62E867256B'
        };
      });

      it('should serialize and parse - full memo, all strings text/plain ', function() {
        input_json.Memos = [
          {
            Memo: {
              MemoType: '74657374',
              MemoFormat: '74657874',
              MemoData: '736F6D652064617461'
            }
          }
        ];

        const so = SerializedObject.from_json(input_json).to_json();
        input_json.Memos[0].Memo.parsed_memo_type = 'test';
        input_json.Memos[0].Memo.parsed_memo_format = 'text';
        input_json.Memos[0].Memo.parsed_memo_data = 'some data';
        input_json.Memos[0].Memo.MemoType = convertStringToHex('test');
        input_json.Memos[0].Memo.MemoFormat = convertStringToHex('text');
        input_json.Memos[0].Memo.MemoData = convertStringToHex('some data');

        assert.deepEqual(so, input_json);
      });

      it('should serialize and parse - full memo, all strings, invalid MemoFormat', function() {
        input_json.Memos = [
          {
            'Memo':
            {
              MemoType: '74657374',
              MemoFormat: '6170706C69636174696F6E2F6A736F6E',
              MemoData: '736F6D652064617461'
            }
          }
        ];

        const so = SerializedObject.from_json(input_json).to_json();
        input_json.Memos[0].Memo.parsed_memo_type = 'test';
        input_json.Memos[0].Memo.parsed_memo_format = 'application/json';
        input_json.Memos[0].Memo.MemoType = convertStringToHex('test');
        input_json.Memos[0].Memo.MemoFormat = convertStringToHex('application/json');
        input_json.Memos[0].Memo.MemoData = convertStringToHex('some data');

        assert.deepEqual(so, input_json);
        assert.strictEqual(input_json.Memos[0].Memo.parsed_memo_data, undefined);
      });

      it('should serialize and parse - full memo, json data, valid MemoFormat, ignored field', function() {
        input_json.Memos = [
          {
            Memo: {
              MemoType: '74657374',
              MemoFormat: '6A736F6E',
              ignored: 'ignored',
              MemoData: '7B22737472696E67223A22736F6D655F737472696E67222C22626F6F6C65616E223A747275657D'
            }
          }
        ];

        const so = SerializedObject.from_json(input_json).to_json();
        delete input_json.Memos[0].Memo.ignored;
        input_json.Memos[0].Memo.parsed_memo_type = 'test';
        input_json.Memos[0].Memo.parsed_memo_format = 'json';
        input_json.Memos[0].Memo.parsed_memo_data = {
          'string': 'some_string',
          'boolean': true
        };
        input_json.Memos[0].Memo.MemoType = convertStringToHex('test');
        input_json.Memos[0].Memo.MemoFormat = convertStringToHex('json');
        input_json.Memos[0].Memo.MemoData = convertStringToHex(JSON.stringify(
          {
            'string': 'some_string',
            'boolean': true
          }
        ));

        assert.deepEqual(so, input_json);
      });

      it('should throw an error - invalid Memo field', function() {
        input_json.Memos = [
          {
            Memo: {
              MemoType: '74657374',
              MemoField: '6A736F6E',
              MemoData: '7B22737472696E67223A22736F6D655F737472696E67222C22626F6F6C65616E223A747275657D'
            }
          }
        ];

        assert.throws(function() {
          SerializedObject.from_json(input_json);
        }, /^Error: JSON contains unknown field: "MemoField" \(Memo\) \(Memos\)/);
      });


      it('should serialize json with memo - match hex output', function() {
        input_json = {
          Flags: 2147483648,
          TransactionType: 'Payment',
          Account: 'rhXzSyt1q9J8uiFXpK3qSugAAPJKXLtnrF',
          Amount: '1',
          Destination: 'radqi6ppXFxVhJdjzaATRBxdrPcVTf1Ung',
          Memos: [
            {
              Memo: {
                MemoType: '696D616765'
              }
            }
          ],
          Sequence: 294,
          SigningPubKey: '03D642E6457B8AB4D140E2C66EB4C484FAFB1BF267CB578EC4815FE6CD06379C51',
          Fee: '12000',
          LastLedgerSequence: 10404607,
          TxnSignature: '304402206B53EDFA6EFCF6FE5BA76C81BABB60A3B55E9DE8A1462DEDC5F387879575E498022015AE7B59AA49E735D7F2E252802C4406CD00689BCE5057C477FE979D38D2DAC9'
        };

        const serializedHex = '12000022800000002400000126201B009EC2FF614000000000000001684000000000002EE0732103D642E6457B8AB4D140E2C66EB4C484FAFB1BF267CB578EC4815FE6CD06379C517446304402206B53EDFA6EFCF6FE5BA76C81BABB60A3B55E9DE8A1462DEDC5F387879575E498022015AE7B59AA49E735D7F2E252802C4406CD00689BCE5057C477FE979D38D2DAC9811426C4CFB3BD05A9AA23936F2E81634C66A9820C9483143DD06317D19C6110CAFF150AE528F58843BE2CA1F9EA7C05696D616765E1F1';
        assert.strictEqual(SerializedObject.from_json(input_json).to_hex(), serializedHex);

      });

    });

  });

});

// vim:sw=2:sts=2:ts=8:et
