/* eslint-disable max-len */
'use strict';
const _ = require('lodash');
const hashes = require('../../hashes');
const addresses = require('../../addresses');
const SerializedObject = require('../../../../src/core').SerializedObject;
const AccountSet = require('./tx/account-set.json');
const NotFound = require('./tx/not-found.json');

module.exports = function(request, options={}) {
  _.defaults(options, {
    memos: [{
      Memo: {
        MemoFormat: '7274312E352E32',
        MemoType: '636C69656E74'
      }
    }],
    hash: hashes.VALID_TRANSACTION_HASH,
    validated: true
  });

  let tx = {
    Account: addresses.ACCOUNT,
    Amount: {
      currency: 'USD',
      issuer: addresses.ISSUER,
      value: '0.001'
    },
    Destination: addresses.ISSUER,
    Fee: '10',
    Flags: 0,
    Memos: options.memos,
    Paths: [
      [
        {
          currency: 'USD',
          issuer: addresses.OTHER_ACCOUNT,
          type: 48,
          type_hex: '0000000000000030'
        },
        {
          account: addresses.OTHER_ACCOUNT,
          currency: 'USD',
          issuer: addresses.OTHER_ACCOUNT,
          type: 49,
          type_hex: '0000000000000031'
        }
      ]
    ],
    SendMax: '1112209',
    Sequence: 4,
    SigningPubKey: '02BC8C02199949B15C005B997E7C8594574E9B02BA2D0628902E0532989976CF9D',
    TransactionType: 'Payment',
    TxnSignature: '304502204EE3E9D1B01D8959B08450FCA9E22025AF503DEF310E34A93863A85CAB3C0BC5022100B61F5B567F77026E8DEED89EED0B7CAF0E6C96C228A2A65216F0DC2D04D52083'
  };

  let meta = {
    AffectedNodes: [
      {
        ModifiedNode: {
          FinalFields: {
            Account: addresses.ACCOUNT,
            BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5E03E788E09BB000',
            BookNode: '0000000000000000',
            Flags: 0,
            OwnerNode: '0000000000000000',
            Sequence: 58,
            TakerGets: {
              currency: 'USD',
              issuer: addresses.OTHER_ACCOUNT,
              value: '5.648998'
            },
            TakerPays: '6208248802'
          },
          LedgerEntryType: 'Offer',
          LedgerIndex: '3CFB3C79D4F1BDB1EE5245259372576D926D9A875713422F7169A6CC60AFA68B',
          PreviousFields: {
            TakerGets: {
              currency: 'USD',
              issuer: addresses.OTHER_ACCOUNT,
              value: '5.65'
            },
            TakerPays: '6209350000'
          },
          PreviousTxnID: '8F571C346688D89AC1F737AE3B6BB5D976702B171CC7B4DE5CA3D444D5B8D6B4',
          PreviousTxnLgrSeq: 348433
        }
      },
      {
        ModifiedNode: {
          FinalFields: {
            Balance: {
              currency: 'USD',
              issuer: 'rrrrrrrrrrrrrrrrrrrrBZbvji',
              value: '-0.001'
            },
            Flags: 131072,
            HighLimit: {
              currency: 'USD',
              issuer: addresses.ISSUER,
              value: '1'
            },
            HighNode: '0000000000000000',
            LowLimit: {
              currency: 'USD',
              issuer: addresses.OTHER_ACCOUNT,
              value: '0'
            },
            LowNode: '0000000000000002'
          },
          LedgerEntryType: 'RippleState',
          LedgerIndex: '4BD1874F8F3A60EDB0C23F5BD43E07953C2B8741B226648310D113DE2B486F01',
          PreviousFields: {
            Balance: {
              currency: 'USD',
              issuer: 'rrrrrrrrrrrrrrrrrrrrBZbvji',
              value: '0'
            }
          },
          PreviousTxnID: '5B2006DAD0B3130F57ACF7CC5CCAC2EEBCD4B57AAA091A6FD0A24B073D08ABB8',
          PreviousTxnLgrSeq: 343703
        }
      },
      {
        ModifiedNode: {
          FinalFields: {
            Account: addresses.ACCOUNT,
            Balance: '9998898762',
            Flags: 0,
            OwnerCount: 3,
            Sequence: 5
          },
          LedgerEntryType: 'AccountRoot',
          LedgerIndex: '4F83A2CF7E70F77F79A307E6A472BFC2585B806A70833CCD1C26105BAE0D6E05',
          PreviousFields: {
            Balance: '9999999970',
            Sequence: 4
          },
          PreviousTxnID: '53354D84BAE8FDFC3F4DA879D984D24B929E7FEB9100D2AD9EFCD2E126BCCDC8',
          PreviousTxnLgrSeq: 343570
        }
      },
      {
        ModifiedNode: {
          FinalFields: {
            Account: 'r9tGqzZgKxVFvzKFdUqXAqTzazWBUia8Qr',
            Balance: '912695302618',
            Flags: 0,
            OwnerCount: 10,
            Sequence: 59
          },
          LedgerEntryType: 'AccountRoot',
          LedgerIndex: 'F3E119AAA87AF3607CF87F5523BB8278A83BCB4142833288305D767DD30C392A',
          PreviousFields: {
            Balance: '912694201420'
          },
          PreviousTxnID: '8F571C346688D89AC1F737AE3B6BB5D976702B171CC7B4DE5CA3D444D5B8D6B4',
          PreviousTxnLgrSeq: 348433
        }
      },
      {
        ModifiedNode: {
          FinalFields: {
            Balance: {
              currency: 'USD',
              issuer: 'rrrrrrrrrrrrrrrrrrrrBZbvji',
              value: '-5.5541638883365'
            },
            Flags: 131072,
            HighLimit: {
              currency: 'USD',
              issuer: 'r9tGqzZgKxVFvzKFdUqXAqTzazWBUia8Qr',
              value: '1000'
            },
            HighNode: '0000000000000000',
            LowLimit: {
              currency: 'USD',
              issuer: addresses.OTHER_ACCOUNT,
              value: '0'
            },
            LowNode: '000000000000000C'
          },
          LedgerEntryType: 'RippleState',
          LedgerIndex: 'FA1255C2E0407F1945BCF9351257C7C5C28B0F5F09BB81C08D35A03E9F0136BC',
          PreviousFields: {
            Balance: {
              currency: 'USD',
              issuer: 'rrrrrrrrrrrrrrrrrrrrBZbvji',
              value: '-5.5551658883365'
            }
          },
          PreviousTxnID: '8F571C346688D89AC1F737AE3B6BB5D976702B171CC7B4DE5CA3D444D5B8D6B4',
          PreviousTxnLgrSeq: 348433
        }
      }
    ],
    TransactionIndex: 0,
    TransactionResult: 'tesSUCCESS'
  };

  let marker = Number(request.marker) || 0;
  marker += 1;
  if (marker === 5) {
    meta.TransactionResult = 'tecINSUFFICIENT_RESERVE';
  } else if (marker === 6) {
    tx = _.cloneDeep(AccountSet.result);
    meta = tx.meta;
    delete tx.meta;
  } else if (marker === 7) {
    tx.Account = addresses.OTHER_ACCOUNT;
  } else if (marker === 8) {
    tx.Destination = addresses.THIRD_ACCOUNT;
  } else if (marker > 25) {
    marker = undefined;
  } else if (marker > 15) {
    tx.Account = addresses.ISSUER;
    tx.Destination = addresses.ACCOUNT;
  }
  if (request.limit === 13) {
    const res = _.assign({}, NotFound, {id: request.id});
    return JSON.stringify(res);
  }
  return JSON.stringify({
    id: request.id,
    status: 'success',
    type: 'response',
    result: {
      marker: marker === undefined ? undefined : String(marker),
      transactions: [
        {
          ledger_index: 348860 - Number(marker || 100),
          tx_blob: SerializedObject.from_json(tx).to_hex(),
          meta: SerializedObject.from_json(meta).to_hex(),
          validated: options.validated
        }
      ]
    }
  });
};
