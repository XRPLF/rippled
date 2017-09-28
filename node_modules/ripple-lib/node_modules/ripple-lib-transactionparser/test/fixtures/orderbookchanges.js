exports.offerCreateNoChangeTakerGets = function() {
  return {
    "TransactionType": "OfferCreate",
    "Flags": 655360,
    "SourceTag": 83,
    "Sequence": 125626,
    "LastLedgerSequence": 14873632,
    "AccountTxnID": "02163187DAED3169C2846261F2E941582B4D27F421EBDFEC7EF888FED00172EC",
    "TakerPays": {
      "value": "209.43838445499",
      "currency": "CNY",
      "issuer": "rnuF96W4SZoCJmbHYBFoJZpR8eCaxNvekK"
    },
    "TakerGets": {
      "value": "0.129333621739",
      "currency": "BTC",
      "issuer": "rG6FZ31hDHN1K5Dkbma3PSB5uVCuVVRzfn"
    },
    "Fee": "10000",
    "SigningPubKey": "EDE30BA017ED458B9B372295863B042C2BA8F11AD53B4BDFB398E778CB7679146B",
    "TxnSignature": "2D6544B45F67F66DF18DE26732F476A270D77D5EC53B732076B71CD5F1605081A2654B8744504739A4423CD2876BF20EEA57BA5F5CC5DD350B22B260D44E470F",
    "Account": "rapido5rxPmP4YkMZZEeXSHqWefxHEkqv6",
    "Memos": [
      {
        "Memo": {
          "MemoData": "CD567EA4D1179162E7EF273B9955D0FE1464D3040050823B409BD5351EB6ACAC3FBBC3FC42B2AD8C4068265B76A2C3353FF00000000000003FF0000000000000"
        }
      }
    ],
    "date": 491243570,
    "hash": "04DB1D743926DF6F2C3855C95FBB877186F55E9A3B25B1B491F55CB84594FC88",
    "inLedger": 14873632,
    "ledger_index": 14873632,
    "meta": {
      "TransactionIndex": 1,
      "AffectedNodes": [
        {
          "ModifiedNode": {
            "LedgerEntryType": "Offer",
            "PreviousTxnLgrSeq": 14873617,
            "PreviousTxnID": "3E527048A0D76BE771AD5945BBE42016A5B739D268378333289C39D62B447BF4",
            "LedgerIndex": "04B97B00F33B261DCD1089B1A094E54ED8F25611E1E6A97D2BB1E071909B58C0",
            "PreviousFields": {
              "TakerPays": {
                "value": "1.7205831",
                "currency": "BTC",
                "issuer": "rG6FZ31hDHN1K5Dkbma3PSB5uVCuVVRzfn"
              }
            },
            "FinalFields": {
              "Flags": 131072,
              "Sequence": 4727,
              "BookNode": "0000000000000000",
              "OwnerNode": "0000000000000000",
              "BookDirectory": "650E5A25B71EDDAF8E865B03D1F9C293039F7D919E59196E4A0A4E71B27E5000",
              "TakerPays": {
                "value": "1.720583099997159",
                "currency": "BTC",
                "issuer": "rG6FZ31hDHN1K5Dkbma3PSB5uVCuVVRzfn"
              },
              "TakerGets": "59310000000",
              "Account": "rDqQUzKUXWgcJbzwjrGw1fZvGEN5dffQYr"
            }
          }
        },
        {
          "ModifiedNode": {
            "LedgerEntryType": "RippleState",
            "PreviousTxnLgrSeq": 14873585,
            "PreviousTxnID": "0595A5E357FC561E0124B51CE9B1315CF81B651516B166595EEA99E98F99CB67",
            "LedgerIndex": "4076B3E46BC7395A7B44AEBB2C1B09EFE3BEFCAD3224FF2663A8725308A726D5",
            "PreviousFields": {
              "Balance": {
                "value": "0",
                "currency": "CNY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji"
              }
            },
            "FinalFields": {
              "Flags": 131072,
              "LowNode": "000000000000010F",
              "HighNode": "0000000000000000",
              "Balance": {
                "value": "-193.198664968391",
                "currency": "CNY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji"
              },
              "LowLimit": {
                "value": "0",
                "currency": "CNY",
                "issuer": "rnuF96W4SZoCJmbHYBFoJZpR8eCaxNvekK"
              },
              "HighLimit": {
                "value": "0",
                "currency": "CNY",
                "issuer": "rapido5rxPmP4YkMZZEeXSHqWefxHEkqv6"
              }
            }
          }
        },
        {
          "DeletedNode": {
            "LedgerEntryType": "DirectoryNode",
            "LedgerIndex": "44E9667BF19A921E0BBDDF6802BCFD00F16988E3ECA2150C5113F1C9564CCFB6",
            "FinalFields": {
              "Flags": 0,
              "ExchangeRate": "5113F1C9564CCFB6",
              "RootIndex": "44E9667BF19A921E0BBDDF6802BCFD00F16988E3ECA2150C5113F1C9564CCFB6",
              "TakerPaysCurrency": "0000000000000000000000004254430000000000",
              "TakerPaysIssuer": "AC4238AB07F0FA4CC4AD8EA53127EF0BE5A5E207",
              "TakerGetsCurrency": "000000000000000000000000434E590000000000",
              "TakerGetsIssuer": "35DD7DF146893456296BF4061FBE68735D28F328"
            }
          }
        },
        {
          "ModifiedNode": {
            "LedgerEntryType": "RippleState",
            "PreviousTxnLgrSeq": 14852798,
            "PreviousTxnID": "7966A2BEFE7DED8D55E2513B943143810FA7AC40AAB31BEDFCC415D7E23FCC1F",
            "LedgerIndex": "477E841FBB209C29E3E42B80C75136EE9792FFE69AA71450B9B9DC42AF73195A",
            "PreviousFields": {
              "Balance": {
                "value": "0.5675541400002614",
                "currency": "BTC",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji"
              }
            },
            "FinalFields": {
              "Flags": 1114112,
              "LowNode": "0000000000000000",
              "HighNode": "0000000000000018",
              "Balance": {
                "value": "0.5675541400031025",
                "currency": "BTC",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji"
              },
              "LowLimit": {
                "value": "1000000000",
                "currency": "BTC",
                "issuer": "rDqQUzKUXWgcJbzwjrGw1fZvGEN5dffQYr"
              },
              "HighLimit": {
                "value": "0",
                "currency": "BTC",
                "issuer": "rG6FZ31hDHN1K5Dkbma3PSB5uVCuVVRzfn"
              }
            }
          }
        },
        {
          "ModifiedNode": {
            "LedgerEntryType": "RippleState",
            "PreviousTxnLgrSeq": 14873632,
            "PreviousTxnID": "02163187DAED3169C2846261F2E941582B4D27F421EBDFEC7EF888FED00172EC",
            "LedgerIndex": "540E2C8BE8CAF4789925E7D0A994AFC9B0BB3384204ABED5273AA1EC570619A8",
            "PreviousFields": {
              "Balance": {
                "value": "0.1084592497698411",
                "currency": "BTC",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji"
              }
            },
            "FinalFields": {
              "Flags": 65536,
              "LowNode": "0000000000000000",
              "HighNode": "0000000000000015",
              "Balance": {
                "value": "0",
                "currency": "BTC",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji"
              },
              "LowLimit": {
                "value": "0",
                "currency": "BTC",
                "issuer": "rapido5rxPmP4YkMZZEeXSHqWefxHEkqv6"
              },
              "HighLimit": {
                "value": "0",
                "currency": "BTC",
                "issuer": "rG6FZ31hDHN1K5Dkbma3PSB5uVCuVVRzfn"
              }
            }
          }
        },
        {
          "ModifiedNode": {
            "LedgerEntryType": "RippleState",
            "PreviousTxnLgrSeq": 14873629,
            "PreviousTxnID": "FD7652E70C7DDBD4675F674DF2EB237907880F6F8EE553B1DBDA9D0460FAF7FD",
            "LedgerIndex": "781AF1DC042C46599EC0BC36614D59AE8D69E6D2BAA9E7B5B31669BADCD2308E",
            "PreviousFields": {
              "Balance": {
                "value": "-4098.164659386341",
                "currency": "CNY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji"
              }
            },
            "FinalFields": {
              "Flags": 2228224,
              "LowNode": "00000000000000FF",
              "HighNode": "0000000000000ACA",
              "Balance": {
                "value": "-3904.96599441795",
                "currency": "CNY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji"
              },
              "LowLimit": {
                "value": "0",
                "currency": "CNY",
                "issuer": "rnuF96W4SZoCJmbHYBFoJZpR8eCaxNvekK"
              },
              "HighLimit": {
                "value": "10000",
                "currency": "CNY",
                "issuer": "rK5j9n8baXfL4gzUoZsfxBvvsv97P5swaV"
              }
            }
          }
        },
        {
          "ModifiedNode": {
            "LedgerEntryType": "RippleState",
            "PreviousTxnLgrSeq": 14853017,
            "PreviousTxnID": "5C040183A84A20547715B222B6526A835B7D9A1F2C7C56FF69C9408BCE95EA95",
            "LedgerIndex": "A828394ABAD75CBE8783A1FFF15CCA2A754A127841CA9BB8A8B118AD001A2DA8",
            "PreviousFields": {
              "Balance": {
                "value": "-7.824035319316732",
                "currency": "BTC",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji"
              }
            },
            "FinalFields": {
              "Flags": 2228224,
              "LowNode": "0000000000000016",
              "HighNode": "0000000000013E29",
              "Balance": {
                "value": "-7.932494569083732",
                "currency": "BTC",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji"
              },
              "LowLimit": {
                "value": "0",
                "currency": "BTC",
                "issuer": "rG6FZ31hDHN1K5Dkbma3PSB5uVCuVVRzfn"
              },
              "HighLimit": {
                "value": "5",
                "currency": "BTC",
                "issuer": "rK5j9n8baXfL4gzUoZsfxBvvsv97P5swaV"
              }
            }
          }
        },
        {
          "ModifiedNode": {
            "LedgerEntryType": "AccountRoot",
            "PreviousTxnLgrSeq": 14873632,
            "PreviousTxnID": "02163187DAED3169C2846261F2E941582B4D27F421EBDFEC7EF888FED00172EC",
            "LedgerIndex": "BFF40FB02870A44349BB5E482CD2A4AA3415C7E72F4D2E9E98129972F26DA9AA",
            "PreviousFields": {
              "Sequence": 125626,
              "AccountTxnID": "02163187DAED3169C2846261F2E941582B4D27F421EBDFEC7EF888FED00172EC",
              "Balance": "930207200763"
            },
            "FinalFields": {
              "Flags": 0,
              "Sequence": 125627,
              "OwnerCount": 57,
              "AccountTxnID": "04DB1D743926DF6F2C3855C95FBB877186F55E9A3B25B1B491F55CB84594FC88",
              "Balance": "930207190763",
              "Account": "rapido5rxPmP4YkMZZEeXSHqWefxHEkqv6"
            }
          }
        },
        {
          "ModifiedNode": {
            "LedgerEntryType": "DirectoryNode",
            "LedgerIndex": "D72D9C23FE838EE26BE81AAD04B7010526F40BFB4920FA5517668017C6CEBF19",
            "FinalFields": {
              "Flags": 0,
              "IndexNext": "0000000000020444",
              "IndexPrevious": "0000000000020442",
              "RootIndex": "D575497E1D19A470AC9B9A02207406EA99D46F828D0250C81BFA1C77505100A3",
              "Owner": "rK5j9n8baXfL4gzUoZsfxBvvsv97P5swaV"
            }
          }
        },
        {
          "ModifiedNode": {
            "LedgerEntryType": "AccountRoot",
            "PreviousTxnLgrSeq": 14873630,
            "PreviousTxnID": "E5C0671123289F10892BF125D26BE4A85358E82E547EEA988823351D5857311F",
            "LedgerIndex": "E8FF8B7DFBB38B1527F66827DC06BF425609099CB89A43DD4C9EBACBCCD8F0DD",
            "PreviousFields": {
              "OwnerCount": 170
            },
            "FinalFields": {
              "Flags": 0,
              "Sequence": 5276287,
              "OwnerCount": 169,
              "EmailHash": "B0000000000000000000000000000000",
              "Balance": "1999599960",
              "Account": "rK5j9n8baXfL4gzUoZsfxBvvsv97P5swaV",
              "RegularKey": "raBRyZ8V4sRbutg6W9vxXMSWdxBqmDTxHR"
            }
          }
        },
        {
          "DeletedNode": {
            "LedgerEntryType": "Offer",
            "LedgerIndex": "F51B76C3119FE31E58229546CA6E78EB7EA4CF0BB6B6C7F6301657417E140752",
            "PreviousFields": {
              "TakerPays": {
                "value": "0.108459249767",
                "currency": "BTC",
                "issuer": "rG6FZ31hDHN1K5Dkbma3PSB5uVCuVVRzfn"
              },
              "TakerGets": {
                "value": "193.198664968391",
                "currency": "CNY",
                "issuer": "rnuF96W4SZoCJmbHYBFoJZpR8eCaxNvekK"
              }
            },
            "FinalFields": {
              "Flags": 0,
              "Sequence": 5276219,
              "PreviousTxnLgrSeq": 14873464,
              "BookNode": "0000000000000000",
              "OwnerNode": "0000000000020443",
              "PreviousTxnID": "1D01FCCF1DA99AD37275B8918A6CD1E5ABBD1FDE0D6FAEB6FDF5567BD2A8E022",
              "BookDirectory": "44E9667BF19A921E0BBDDF6802BCFD00F16988E3ECA2150C5113F1C9564CCFB6",
              "TakerPays": {
                "value": "0",
                "currency": "BTC",
                "issuer": "rG6FZ31hDHN1K5Dkbma3PSB5uVCuVVRzfn"
              },
              "TakerGets": {
                "value": "0",
                "currency": "CNY",
                "issuer": "rnuF96W4SZoCJmbHYBFoJZpR8eCaxNvekK"
              },
              "Account": "rK5j9n8baXfL4gzUoZsfxBvvsv97P5swaV"
            }
          }
        }
      ],
      "TransactionResult": "tesSUCCESS"
    },
    "validated": true
  };
};

exports.offerCreateConsumedOffer = function() {
  return {
    "Account": "rBxy23n7ZFbUpS699rFVj1V9ZVhAq6EGwC",
    "Fee": "20000",
    "Flags": 131072,
    "Sequence": 609776,
    "SigningPubKey": "03917C08C81FEC424141C50A1C4B7C77A4B1563D51B7FA260797B9717F52C5E6D5",
    "TakerGets": {
      "currency": "BTC",
      "issuer": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
      "value": "0.2167622002262332"
    },
    "TakerPays": {
      "currency": "USD",
      "issuer": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
      "value": "57.5510124906279"
    },
    "TransactionType": "OfferCreate",
    "TxnSignature": "304402207E48A159CBA0491684C8BBE31DEF55859A7616EAA2339C43445CF0185DC20A07022017D442BB2F6AB8BB9925765A690473332D1C1157AE310409D3CFD45755708E6F",
    "date": 474426920,
    "hash": "0D13787384301F32E9E180C31F7F16EA0D2521783DBF71736B25AFF253FB6E11",
    "inLedger": 11086861,
    "ledger_index": 11086861,
    "meta": {
      "AffectedNodes": [
        {
        "DeletedNode": {
          "FinalFields": {
            "ExchangeRate": "520D604D6638790F",
            "Flags": 0,
            "RootIndex": "20294C923E80A51B487EB9547B3835FD483748B170D2D0A4520D604D6638790F",
            "TakerGetsCurrency": "0000000000000000000000005553440000000000",
            "TakerGetsIssuer": "0A20B3C85F482532A9578DBB3950B85CA06594D1",
            "TakerPaysCurrency": "0000000000000000000000004254430000000000",
            "TakerPaysIssuer": "0A20B3C85F482532A9578DBB3950B85CA06594D1"
          },
          "LedgerEntryType": "DirectoryNode",
          "LedgerIndex": "20294C923E80A51B487EB9547B3835FD483748B170D2D0A4520D604D6638790F"
        }
      },
      {
        "DeletedNode": {
          "FinalFields": {
            "Account": "r49y2xKuKVG2dPkNHgWQAV61cjxk8gryjQ",
            "BookDirectory": "20294C923E80A51B487EB9547B3835FD483748B170D2D0A4520D604D6638790F",
            "BookNode": "0000000000000000",
            "Flags": 0,
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "97AA291851DE9A894CFCCD4C69C96E9570F9182A5D39937463E1C80132DD65DE",
            "PreviousTxnLgrSeq": 11086861,
            "Sequence": 550,
            "TakerGets": {
              "currency": "USD",
              "issuer": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
              "value": "0"
            },
            "TakerPays": {
              "currency": "BTC",
              "issuer": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
              "value": "0"
            }
          },
          "LedgerEntryType": "Offer",
          "LedgerIndex": "276522C8AAF28B5286C48E2373C119C48DAE78C3F8A047AAF67C22E4440C391B",
          "PreviousFields": {
            "TakerGets": {
              "currency": "USD",
              "issuer": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
              "value": "0.0000000036076"
            },
            "TakerPays": {
              "currency": "BTC",
              "issuer": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
              "value": "1358360000000000e-26"
            }
          }
        }
      },
      {
        "ModifiedNode": {
          "FinalFields": {
            "Flags": 0,
            "Owner": "r49y2xKuKVG2dPkNHgWQAV61cjxk8gryjQ",
            "RootIndex": "38D499A08201B64C001CF6B1803504373BFDA21A01302D3C0E78EF98544E9236"
          },
          "LedgerEntryType": "DirectoryNode",
          "LedgerIndex": "38D499A08201B64C001CF6B1803504373BFDA21A01302D3C0E78EF98544E9236"
        }
      },
      {
        "ModifiedNode": {
          "FinalFields": {
            "Balance": {
              "currency": "BTC",
              "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
              "value": "-0.1151236147503502"
            },
            "Flags": 2228224,
            "HighLimit": {
              "currency": "BTC",
              "issuer": "rBxy23n7ZFbUpS699rFVj1V9ZVhAq6EGwC",
              "value": "0"
            },
            "HighNode": "0000000000000000",
            "LowLimit": {
              "currency": "BTC",
              "issuer": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
              "value": "0"
            },
            "LowNode": "000000000000028F"
          },
          "LedgerEntryType": "RippleState",
          "LedgerIndex": "42A6E9991D540C80BE4A43EF5254656DD862F602BBFF99BC576B44FBF6B7D775",
          "PreviousFields": {
            "Balance": {
              "currency": "BTC",
              "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
              "value": "-0.3322932790173214"
            }
          },
          "PreviousTxnID": "B7CE60D440E11F31530E19A50A0775246102425D3594C9B886A7724BB1E58367",
          "PreviousTxnLgrSeq": 11086861
        }
      },
      {
        "ModifiedNode": {
          "FinalFields": {
            "Balance": {
              "currency": "USD",
              "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
              "value": "-23112.9993818472"
            },
            "Flags": 2228224,
            "HighLimit": {
              "currency": "USD",
              "issuer": "r49y2xKuKVG2dPkNHgWQAV61cjxk8gryjQ",
              "value": "1000000000"
            },
            "HighNode": "0000000000000000",
            "LowLimit": {
              "currency": "USD",
              "issuer": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
              "value": "0"
            },
            "LowNode": "0000000000000231"
          },
          "LedgerEntryType": "RippleState",
          "LedgerIndex": "615463C4F78931AA3E2B65FE49C6DAAC25A456C15679E67D1C19CA0943D98C5A",
          "PreviousFields": {
            "Balance": {
              "currency": "USD",
              "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
              "value": "-23112.99938185081"
            }
          },
          "PreviousTxnID": "97AA291851DE9A894CFCCD4C69C96E9570F9182A5D39937463E1C80132DD65DE",
          "PreviousTxnLgrSeq": 11086861
        }
      },
      {
        "ModifiedNode": {
          "FinalFields": {
            "Balance": {
              "currency": "BTC",
              "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
              "value": "-20.18770947118515"
            },
            "Flags": 131072,
            "HighLimit": {
              "currency": "BTC",
              "issuer": "r49y2xKuKVG2dPkNHgWQAV61cjxk8gryjQ",
              "value": "0"
            },
            "HighNode": "0000000000000000",
            "LowLimit": {
              "currency": "BTC",
              "issuer": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
              "value": "0"
            },
            "LowNode": "00000000000002C4"
          },
          "LedgerEntryType": "RippleState",
          "LedgerIndex": "817EB23FB16D8D17676F29055C989CDFB738B7FC310DF3AB5CA0D06AA2DC1326",
          "PreviousFields": {
            "Balance": {
              "currency": "BTC",
              "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
              "value": "-20.18770947117157"
            }
          },
          "PreviousTxnID": "97AA291851DE9A894CFCCD4C69C96E9570F9182A5D39937463E1C80132DD65DE",
          "PreviousTxnLgrSeq": 11086861
        }
      },
      {
        "ModifiedNode": {
          "FinalFields": {
            "Balance": {
              "currency": "BTC",
              "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
              "value": "-42.47198893790961"
            },
            "Flags": 2228224,
            "HighLimit": {
              "currency": "BTC",
              "issuer": "rQE5Z3FgVnRMbVfS6xiVQFgB4J3X162FVD",
              "value": "150"
            },
            "HighNode": "0000000000000000",
            "LowLimit": {
              "currency": "BTC",
              "issuer": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
              "value": "0"
            },
            "LowNode": "0000000000000201"
          },
          "LedgerEntryType": "RippleState",
          "LedgerIndex": "C688AE8E51943530C931C3B838D15818BDA1F1B60B641B5F866B724AD7D3E79B",
          "PreviousFields": {
            "Balance": {
              "currency": "BTC",
              "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
              "value": "-42.25525274603999"
            }
          },
          "PreviousTxnID": "1C749407E3676E77693694BEBC73C74196EA39C4EB2BB47781ABD65F4AB315E9",
          "PreviousTxnLgrSeq": 11082323
        }
      },
      {
        "ModifiedNode": {
          "FinalFields": {
            "Balance": {
              "currency": "USD",
              "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
              "value": "-283631.3541172556"
            },
            "Flags": 2228224,
            "HighLimit": {
              "currency": "USD",
              "issuer": "rQE5Z3FgVnRMbVfS6xiVQFgB4J3X162FVD",
              "value": "5000000"
            },
            "HighNode": "0000000000000000",
            "LowLimit": {
              "currency": "USD",
              "issuer": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
              "value": "0"
            },
            "LowNode": "0000000000000201"
          },
          "LedgerEntryType": "RippleState",
          "LedgerIndex": "D8F66B71771581E6185072E5264B2C4C0F9C2CA642EE46B62D6F550D897D00FF",
          "PreviousFields": {
            "Balance": {
              "currency": "USD",
              "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
              "value": "-283689.0202317675"
            }
          },
          "PreviousTxnID": "0419F004A3084E93D4708EDA40D64A9F52F52EAA854961C23E2779EBE400AAD9",
          "PreviousTxnLgrSeq": 11086605
        }
      },
      {
        "ModifiedNode": {
          "FinalFields": {
            "Account": "r49y2xKuKVG2dPkNHgWQAV61cjxk8gryjQ",
            "Balance": "52083119197",
            "Flags": 0,
            "OwnerCount": 8,
            "Sequence": 553
          },
          "LedgerEntryType": "AccountRoot",
          "LedgerIndex": "DD314C9308B172885F6D0F5F3F50A2EAB1D2E2BD75A65A4236547E9C1DD625DB",
          "PreviousFields": {
            "OwnerCount": 9
          },
          "PreviousTxnID": "F07EA8FA7FF285FA5EC5F5A36CCCFC0F3D4B9A9A2910EEABABF058F96F6CD402",
          "PreviousTxnLgrSeq": 11082743
        }
      },
      {
        "ModifiedNode": {
          "FinalFields": {
            "Balance": {
              "currency": "USD",
              "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
              "value": "-57.5510124906279"
            },
            "Flags": 2228224,
            "HighLimit": {
              "currency": "USD",
              "issuer": "rBxy23n7ZFbUpS699rFVj1V9ZVhAq6EGwC",
              "value": "0"
            },
            "HighNode": "0000000000000000",
            "LowLimit": {
              "currency": "USD",
              "issuer": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
              "value": "0"
            },
            "LowNode": "000000000000028F"
          },
          "LedgerEntryType": "RippleState",
          "LedgerIndex": "E929BE69F05FEB6B376C97E22A264D93D88A7E42BE3FE5BFBD1842AC08C85BCF",
          "PreviousFields": {
            "Balance": {
              "currency": "USD",
              "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
              "value": "0"
            }
          },
          "PreviousTxnID": "73867036670B2F95ADCFF006A253C700ED45EF83F1B125D4797F2C110B055B60",
          "PreviousTxnLgrSeq": 11086861
        }
      },
      {
        "ModifiedNode": {
          "FinalFields": {
            "Account": "rQE5Z3FgVnRMbVfS6xiVQFgB4J3X162FVD",
            "BookDirectory": "20294C923E80A51B487EB9547B3835FD483748B170D2D0A4520D61247A328674",
            "BookNode": "0000000000000000",
            "Flags": 0,
            "OwnerNode": "000000000000001B",
            "Sequence": 114646,
            "TakerGets": {
              "currency": "USD",
              "issuer": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
              "value": "0.00000002162526"
            },
            "TakerPays": {
              "currency": "BTC",
              "issuer": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
              "value": "8144010000000000e-26"
            }
          },
          "LedgerEntryType": "Offer",
          "LedgerIndex": "E9F98B8933C500737D5FD0BCAFC49EADB8F8A9D01170EFB7CA171D0DEF853D02",
          "PreviousFields": {
            "TakerGets": {
              "currency": "USD",
              "issuer": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
              "value": "57.55101250864556"
            },
            "TakerPays": {
              "currency": "BTC",
              "issuer": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
              "value": "0.2167361919510613"
            }
          },
          "PreviousTxnID": "23433B9508778BEE0E8CE398602BBEDAFAE210F59979BCAC818B6970DCCB91F5",
          "PreviousTxnLgrSeq": 11080258
        }
      },
      {
        "ModifiedNode": {
          "FinalFields": {
            "Account": "rBxy23n7ZFbUpS699rFVj1V9ZVhAq6EGwC",
            "Balance": "267312570945",
            "Flags": 0,
            "OwnerCount": 25,
            "Sequence": 609777
          },
          "LedgerEntryType": "AccountRoot",
          "LedgerIndex": "EAFF4A0B5E891B9BE6A4D484FD0A73356F099FA54F650C9D8FB35D3F29A44176",
          "PreviousFields": {
            "Balance": "267312590945",
            "Sequence": 609776
          },
          "PreviousTxnID": "B7CE60D440E11F31530E19A50A0775246102425D3594C9B886A7724BB1E58367",
          "PreviousTxnLgrSeq": 11086861
        }
      }
      ],
      "TransactionIndex": 48,
      "TransactionResult": "tesSUCCESS"
    },
    "validated": true
  };
};

exports.offerCreateCreatedOffer = function() {
  return {
    "Account": "rEQWVz1qN4DWw5J17s3DgXQzUuVYDSpK6M",
    "Fee": "12000",
    "Flags": 0,
    "LastLedgerSequence": 11349682,
    "Memos": [
      {
      "Memo": {
        "MemoData": "7274312E322E31",
        "MemoType": "636C69656E74"
      }
    }
    ],
    "Sequence": 26,
    "SigningPubKey": "039549AB540046941E2BD313CB71F0EEA3A560B587AE4ED75A7120965A67E0D6E1",
    "TakerGets": {
      "currency": "JPY",
      "issuer": "r94s8px6kSw1uZ1MV98dhSRTvc6VMPoPcN",
      "value": "0.0001"
    },
    "TakerPays": "10000000000000",
    "TransactionType": "OfferCreate",
    "TxnSignature": "3044022056A68DDAD0F7568874D5233B408E9D22E50DF464CC1994F5F922E7124BA7719C02202021849173D5B51BA707A7B6A335357B75B375EA016A83914289942F51835AC4",
    "date": 475609770,
    "hash": "D53A3B99AC0C3CAF35D72178390ACA94CD42479A98CEA438EEAFF338E5FEB76D",
    "inLedger": 11349675,
    "ledger_index": 11349675,
    "meta": {
      "AffectedNodes": [
        {
        "CreatedNode": {
          "LedgerEntryType": "Offer",
          "LedgerIndex": "296EE8E1CC21F1122DB7A95EFD3C0BEC5CB1FCB4817573B47734E6EC55090707",
          "NewFields": {
            "Account": "rEQWVz1qN4DWw5J17s3DgXQzUuVYDSpK6M",
            "BookDirectory": "9F72CA02AB7CBA0FD97EA5F245C03EDC555C3FE97749CD4266038D7EA4C68000",
            "Sequence": 26,
            "TakerGets": {
              "currency": "JPY",
              "issuer": "r94s8px6kSw1uZ1MV98dhSRTvc6VMPoPcN",
              "value": "0.0001"
            },
            "TakerPays": "10000000000000"
          }
        }
      },
      {
        "CreatedNode": {
          "LedgerEntryType": "DirectoryNode",
          "LedgerIndex": "9F72CA02AB7CBA0FD97EA5F245C03EDC555C3FE97749CD4266038D7EA4C68000",
          "NewFields": {
            "ExchangeRate": "66038D7EA4C68000",
            "RootIndex": "9F72CA02AB7CBA0FD97EA5F245C03EDC555C3FE97749CD4266038D7EA4C68000",
            "TakerGetsCurrency": "0000000000000000000000004A50590000000000",
            "TakerGetsIssuer": "5BBC0F22F61D9224A110650CFE21CC0C4BE13098"
          }
        }
      },
      {
        "ModifiedNode": {
          "FinalFields": {
            "Account": "rEQWVz1qN4DWw5J17s3DgXQzUuVYDSpK6M",
            "Balance": "59940000",
            "Flags": 0,
            "OwnerCount": 2,
            "Sequence": 27
          },
          "LedgerEntryType": "AccountRoot",
          "LedgerIndex": "C666A91E2D289AB6DD1A44363E1F4714B60584AA79B2CBFBB3330236610E4E47",
          "PreviousFields": {
            "Balance": "59952000",
            "OwnerCount": 1,
            "Sequence": 26
          },
          "PreviousTxnID": "86BD597EE965EB803B9C44BBFD651468076BCF1F982BD1F91D7B2E77BB0BC50A",
          "PreviousTxnLgrSeq": 11349670
        }
      },
      {
        "ModifiedNode": {
          "FinalFields": {
            "Flags": 0,
            "Owner": "rEQWVz1qN4DWw5J17s3DgXQzUuVYDSpK6M",
            "RootIndex": "E8C9FDFB9C7494135DF41ED69DFD0B9747CFE0ADF046E32BA24510B6A1EFDAE0"
          },
          "LedgerEntryType": "DirectoryNode",
          "LedgerIndex": "E8C9FDFB9C7494135DF41ED69DFD0B9747CFE0ADF046E32BA24510B6A1EFDAE0"
        }
      }
      ],
      "TransactionIndex": 11,
      "TransactionResult": "tesSUCCESS"
    },
    "validated": true
  };
};

exports.offerCancel = function() {
  return {
    "Account": "rEQWVz1qN4DWw5J17s3DgXQzUuVYDSpK6M",
    "Fee": "12000",
    "Flags": 0,
    "LastLedgerSequence": 11236701,
    "OfferSequence": 20,
    "Sequence": 22,
    "SigningPubKey": "039549AB540046941E2BD313CB71F0EEA3A560B587AE4ED75A7120965A67E0D6E1",
    "TransactionType": "OfferCancel",
    "TxnSignature": "304402200E24DFA7B5F37675CCBE5370EDB51A8EC4E58D55D34ADC19505DE3EE686ED64B0220421C955F4F4D63DFA517E48F81393FB007035C18821D25D8EA8C36D9A71AF0F4",
    "date": 475105560,
    "hash": "3D948699072B40312AE313E7E8297EED83080C9A4D5B564BCACF0951ABF00AC5",
    "inLedger": 11236693,
    "ledger_index": 11236693,
    "meta": {
      "AffectedNodes": [
        {
        "DeletedNode": {
          "FinalFields": {
            "Account": "rEQWVz1qN4DWw5J17s3DgXQzUuVYDSpK6M",
            "BookDirectory": "9F72CA02AB7CBA0FD97EA5F245C03EDC555C3FE97749CD425B038D7EA4C68000",
            "BookNode": "0000000000000000",
            "Flags": 0,
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "3D768E210A152DFA89C051FEFC26F2FFBF91AC8B794482B8DA906157D3B2C348",
            "PreviousTxnLgrSeq": 11235523,
            "Sequence": 20,
            "TakerGets": {
              "currency": "JPY",
              "issuer": "r94s8px6kSw1uZ1MV98dhSRTvc6VMPoPcN",
              "value": "1000"
            },
            "TakerPays": "1000000000"
          },
          "LedgerEntryType": "Offer",
          "LedgerIndex": "39A270DE16B6861952C5409626B0FA68FCC1089DD242AF55D8B1CAE6194C0E67"
        }
      },
      {
        "ModifiedNode": {
          "FinalFields": {
            "ExchangeRate": "5B038D7EA4C68000",
            "Flags": 0,
            "RootIndex": "9F72CA02AB7CBA0FD97EA5F245C03EDC555C3FE97749CD425B038D7EA4C68000",
            "TakerGetsCurrency": "0000000000000000000000004A50590000000000",
            "TakerGetsIssuer": "5BBC0F22F61D9224A110650CFE21CC0C4BE13098",
            "TakerPaysCurrency": "0000000000000000000000000000000000000000",
            "TakerPaysIssuer": "0000000000000000000000000000000000000000"
          },
          "LedgerEntryType": "DirectoryNode",
          "LedgerIndex": "9F72CA02AB7CBA0FD97EA5F245C03EDC555C3FE97749CD425B038D7EA4C68000"
        }
      },
      {
        "ModifiedNode": {
          "FinalFields": {
            "Account": "rEQWVz1qN4DWw5J17s3DgXQzUuVYDSpK6M",
            "Balance": "29988000",
            "Flags": 0,
            "OwnerCount": 1,
            "Sequence": 23
          },
          "LedgerEntryType": "AccountRoot",
          "LedgerIndex": "C666A91E2D289AB6DD1A44363E1F4714B60584AA79B2CBFBB3330236610E4E47",
          "PreviousFields": {
            "Balance": "30000000",
            "OwnerCount": 2,
            "Sequence": 22
          },
          "PreviousTxnID": "FC061E8B2FAE945F4F674E11D4EF25F3B951DEB9116CDE5506B35EF383DC8988",
          "PreviousTxnLgrSeq": 11235525
        }
      },
      {
        "ModifiedNode": {
          "FinalFields": {
            "Flags": 0,
            "Owner": "rEQWVz1qN4DWw5J17s3DgXQzUuVYDSpK6M",
            "RootIndex": "E8C9FDFB9C7494135DF41ED69DFD0B9747CFE0ADF046E32BA24510B6A1EFDAE0"
          },
          "LedgerEntryType": "DirectoryNode",
          "LedgerIndex": "E8C9FDFB9C7494135DF41ED69DFD0B9747CFE0ADF046E32BA24510B6A1EFDAE0"
        }
      }
      ],
      "TransactionIndex": 2,
      "TransactionResult": "tesSUCCESS"
    },
    "validated": true
  };
};

exports.parsedOfferCreateNoChangeTakerGets = function() {
  return {
    rDqQUzKUXWgcJbzwjrGw1fZvGEN5dffQYr: [
      {
        direction: 'sell',
        totalPrice: {
          currency: 'BTC',
          counterparty: 'rG6FZ31hDHN1K5Dkbma3PSB5uVCuVVRzfn',
          value: '-2.841e-12'
        },
        quantity: {
          currency: 'XRP',
          value: '0'
        },
        makerExchangeRate: '0.00002901',
        sequence: 4727,
        status: 'open'
      }
    ],
    rK5j9n8baXfL4gzUoZsfxBvvsv97P5swaV: [
      {
        direction: 'buy',
        quantity: {
          currency: 'BTC',
          counterparty: 'rG6FZ31hDHN1K5Dkbma3PSB5uVCuVVRzfn',
          value: '-0.108459249767'
        },
        totalPrice: {
          currency: 'CNY',
          counterparty: 'rnuF96W4SZoCJmbHYBFoJZpR8eCaxNvekK',
          value: '-193.198664968391'
        },
        makerExchangeRate: '0.0005613871596097462',
        sequence: 5276219,
        status: 'closed'
      }
    ]
  };
};

exports.parsedOfferCreate = function () {
  return {
    "r49y2xKuKVG2dPkNHgWQAV61cjxk8gryjQ": [
      {
        'direction': 'buy',
        "quantity": {
          "currency": "BTC",
          "counterparty": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
          "value": "-1.35836e-11"
        },
        "totalPrice": {
          "currency": "USD",
          "counterparty": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
          "value": "-3.6076e-9"
        },
        "makerExchangeRate": "0.003765060240963855",
        "sequence": 550,
        "status": "closed"
      }
    ],
    "rQE5Z3FgVnRMbVfS6xiVQFgB4J3X162FVD": [
      {
        'direction': 'buy',
        "quantity": {
          "currency": "BTC",
          "counterparty": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
          "value": "-0.2167361918696212"
        },
        "totalPrice": {
          "currency": "USD",
          "counterparty": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B",
          "value": "-57.5510124870203"
        },
        "makerExchangeRate": "0.003765983994087028",
        "sequence": 114646,
        "status": "open"
      }
    ]
  };
};

exports.parsedOfferCreateCreated = function() {
  return {
    "rEQWVz1qN4DWw5J17s3DgXQzUuVYDSpK6M": [
      {
        'direction': 'buy',
        "quantity": {
          "currency": "XRP",
          "value": "10000000"
        },
        "totalPrice": {
          "currency": "JPY",
          "counterparty": "r94s8px6kSw1uZ1MV98dhSRTvc6VMPoPcN",
          "value": "0.0001"
        },
        "makerExchangeRate": "100000000000",
        "sequence": 26,
        "status": "created"
      }
    ]
  };
};

exports.parsedOfferCancel = function () {
  return {
    "rEQWVz1qN4DWw5J17s3DgXQzUuVYDSpK6M": [
      {
        'direction': 'buy',
        "quantity": {
          "currency": "XRP",
          "value": "0"
        },
        "totalPrice": {
          "currency": "JPY",
          "counterparty": "r94s8px6kSw1uZ1MV98dhSRTvc6VMPoPcN",
          "value": "0"
        },
        "makerExchangeRate": "1",
        "sequence": 20,
        "status": "canceled"
      }
    ]
  };
};
