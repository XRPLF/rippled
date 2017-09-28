/* eslint-disable max-len */
'use strict';
const _ = require('lodash');

module.exports.requestBookOffersBidsResponse = function(request, options={}) {
  _.defaults(options, {
    gets: {
      currency: 'USD',
      issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
    },
    pays: {
      currency: 'BTC',
      issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
    }
  });

  return JSON.stringify({
    id: request.id,
    result: {
      ledger_index: 10716345,
      offers: [
        {
          Account: 'r49y2xKuKVG2dPkNHgWQAV61cjxk8gryjQ',
          BookDirectory: '20294C923E80A51B487EB9547B3835FD483748B170D2D0A4520B15A60037FFCF',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000000',
          PreviousTxnID: '544932DC56D72E845AF2B738821FE07865E32EC196270678AB0D947F54E9F49F',
          PreviousTxnLgrSeq: 10679000,
          Sequence: 434,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '3205.1'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '10'
         },
          index: 'CE457115A4ADCC8CB351B3E35A0851E48DE16605C23E305017A9B697B156DE5A',
          owner_funds: '41952.95917199965',
          quality: '0.003120027456241615'
       },
        {
          Account: 'rDYCRhpahKEhCFV25xScg67Bwf4W9sTYAm',
          BookDirectory: '20294C923E80A51B487EB9547B3835FD483748B170D2D0A4520B1A2BC2EC5000',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000000',
          PreviousTxnID: 'F68F9658AB3D462FEB027E6C380F054BC6D2514B43EC3C6AD46EE19C59BF1CC3',
          PreviousTxnLgrSeq: 10704238,
          Sequence: 233,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '1599.063669386278'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '4.99707396683212'
         },
          index: 'BF14FBB305159DBCAEA91B7E848408F5B559A91B160EBCB6D244958A6A16EA6B',
          owner_funds: '3169.910902910102',
          quality: '0.003125'
       },
        {
          Account: 'raudnGKfTK23YKfnS7ixejHrqGERTYNFXk',
          BookDirectory: '20294C923E80A51B487EB9547B3835FD483748B170D2D0A4520B2BF1C2F4D4C9',
          BookNode: '0000000000000000',
          Expiration: 472785284,
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '00000000000008F0',
          PreviousTxnID: '446410E1CD718AC01929DD16B558FCF6B3A7B8BF208C420E67A280C089C5C59B',
          PreviousTxnLgrSeq: 10713576,
          Sequence: 110104,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '143.1050962074379'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '0.4499999999999999'
         },
          index: '67924B0EAA15784CC00CCD5FDD655EE2D6D2AE40341776B5F14E52341E7FC73E',
          owner_funds: '0',
          quality: '0.003144542101755081',
          taker_gets_funded: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0'
         },
          taker_pays_funded: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '0'
         }
       },
        {
          Account: 'rDVBvAQScXrGRGnzrxRrcJPeNLeLeUTAqE',
          BookDirectory: '20294C923E80A51B487EB9547B3835FD483748B170D2D0A4520B2CD7A2BFBB75',
          BookNode: '0000000000000000',
          Expiration: 472772651,
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '00000000000003CD',
          PreviousTxnID: 'D49164AB68DDA3AEC9DFCC69A35685C4F532B5C231D3C1D25FEA7D5D0224FB84',
          PreviousTxnLgrSeq: 10711128,
          Sequence: 35625,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '254.329207354604'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '0.8'
         },
          index: '567BF2825173E3FB28FC94E436B6EB30D9A415FC2335E6D25CDE1BE47B25D120',
          owner_funds: '0',
          quality: '0.003145529403882357',
          taker_gets_funded: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0'
         },
          taker_pays_funded: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '0'
         }
       },
        {
          Account: 'rwBYyfufTzk77zUSKEu4MvixfarC35av1J',
          BookDirectory: '20294C923E80A51B487EB9547B3835FD483748B170D2D0A4520B3621DF140FDA',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000008',
          PreviousTxnID: '2E371E2B287C8A9FBB3424E4204B17AD9FA1BAA9F3B33C7D2261E3B038AFF083',
          PreviousTxnLgrSeq: 10716291,
          Sequence: 387756,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '390.4979'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '1.23231134568807'
         },
          index: '8CA23E55BF9F46AC7E803D3DB40FD03225EFCA66650D4CF0CBDD28A7CCDC8400',
          owner_funds: '5704.824764087842',
          quality: '0.003155743848271834'
       },
        {
          Account: 'rwjsRktX1eguUr1pHTffyHnC4uyrvX58V1',
          BookDirectory: '20294C923E80A51B487EB9547B3835FD483748B170D2D0A4520B3A4D41FF4211',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000000',
          PreviousTxnID: '91763FA7089C63CC4D5D14CBA6A5A5BF7ECE949B0D34F00FD35E733AF9F05AF1',
          PreviousTxnLgrSeq: 10716292,
          Sequence: 208927,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '1'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '0.003160328237957649'
         },
          index: '7206866E39D9843623EE79E570242753DEE3C597F3856AEFB4631DD5AD8B0557',
          owner_funds: '45.55665106096075',
          quality: '0.003160328237957649'
       },
        {
          Account: 'r49y2xKuKVG2dPkNHgWQAV61cjxk8gryjQ',
          BookDirectory: '20294C923E80A51B487EB9547B3835FD483748B170D2D0A4520B4748E68669A7',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000000',
          PreviousTxnID: '3B3CF6FF1A336335E78513CF77AFD3A784ACDD7B1B4D3F1F16E22957A060BFAE',
          PreviousTxnLgrSeq: 10639969,
          Sequence: 429,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '4725'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '15'
         },
          index: '42894809370C7E6B23498EF8E22AD4B05F02B94F08E6983357A51EA96A95FF7F',
          quality: '0.003174603174603175'
       },
        {
          Account: 'rDbsCJr5m8gHDCNEHCZtFxcXHsD4S9jH83',
          BookDirectory: '20294C923E80A51B487EB9547B3835FD483748B170D2D0A4520B58077ED03C1B',
          BookNode: '0000000000000000',
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000001',
          PreviousTxnID: '98F3F2D02D3BB0AEAC09EECCF2F24BBE5E1AB2C71C40D7BD0A5199E12541B6E2',
          PreviousTxnLgrSeq: 10715839,
          Sequence: 110099,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '1.24252537879871'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.gets.issuer,
            value: '0.003967400879423823'
         },
          index: 'F4404D6547149419D3607F81D7080979FBB3AFE2661F9A933E2F6C07AC1D1F6D',
          owner_funds: '73.52163803897041',
          quality: '0.003193013959408667'
       },
        {
          Account: 'rDVBvAQScXrGRGnzrxRrcJPeNLeLeUTAqE',
          BookDirectory: '20294C923E80A51B487EB9547B3835FD483748B170D2D0A4520B72A555B981A3',
          BookNode: '0000000000000000',
          Expiration: 472772652,
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '00000000000003CD',
          PreviousTxnID: '146C8DBB047BAAFAE5B8C8DECCCDACD9DFCD7A464E5AB273230FF975E9B83CF7',
          PreviousTxnLgrSeq: 10711128,
          Sequence: 35627,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '496.5429474010489'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '1.6'
         },
          index: '50CAA04E81D0009115B61C132FC9887FA9E5336E0CB8A2E7D3280ADBF6ABC043',
          quality: '0.003222279177208227',
          taker_gets_funded: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0'
         },
          taker_pays_funded: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '0'
         }
       },
        {
          Account: 'r49y2xKuKVG2dPkNHgWQAV61cjxk8gryjQ',
          BookDirectory: '20294C923E80A51B487EB9547B3835FD483748B170D2D0A4520B730474DD96E5',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000000',
          PreviousTxnID: '624F9ADA85EC3BE845EAC075B47E01E4F89288EAF27823C715777B3DFFB21F24',
          PreviousTxnLgrSeq: 10639989,
          Sequence: 431,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '3103'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '10'
         },
          index: '8A319A496288228AD9CAD74375E32FA81805C56A9AD84798A26756A8B3F9EE23',
          quality: '0.003222687721559781'
       }
     ],
      validated: false
   },
    status: 'success',
    type: 'response'
 });
};

module.exports.requestBookOffersBidsPartialFundedResponse = function(request, options={}) {
  _.defaults(options, {
    gets: {
      currency: 'BTC',
      issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
    },
    pays: {
      currency: 'USD',
      issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
    }
  });

  return JSON.stringify({
    id: request.id,
    status: 'success',
    type: 'response',
    result: {
      ledger_current_index: 10714274,
      offers: [
        {
          Account: 'rpUirQxhaFqMp7YHPLMZCWxgZQbaZkp4bM',
          BookDirectory: '20294C923E80A51B487EB9547B3835FD483748B170D2D0A4520B75DA97A99CE7',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000000',
          PreviousTxnID: '52801D1249261E410632BF6C00F503B1F51B31798C1E7DBD67B976FE65BE4DA4',
          PreviousTxnLgrSeq: 10630313,
          Sequence: 132,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '310'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '1'
         },
          index: '861D15BECDA5DCA1327CF4D8080C181425F043AC969A992C5FAE5D12813785D0',
          owner_funds: '259.7268806690133',
          quality: '0.003225806451612903',
          taker_gets_funded: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '259.2084637415302'
         },
          taker_pays_funded: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '0.8361563346500974'
         }
       }
     ]
   }
 });
};

module.exports.requestBookOffersAsksPartialFundedResponse = function(request, options={}) {

  _.defaults(options, {
    gets: {
      currency: 'BTC',
      issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
    },
    pays: {
      currency: 'USD',
      issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
    }
  });

  return JSON.stringify({
    id: request.id,
    status: 'success',
    type: 'response',
    result: {
      ledger_current_index: 10714274,
      offers: [
        {
          Account: 'rPyYxUGK8L4dgEvjPs3aRc1B1jEiLr3Hx5',
          BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC98570BCB85BCA78000',
          BookNode: '0000000000000000',
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000000',
          PreviousTxnID: 'D22993C68C94ACE3F2FCE4A334EBEA98CC46DCA92886C12B5E5B4780B5E17D4E',
          PreviousTxnLgrSeq: 10711938,
          Sequence: 392,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0.8095'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '268.754'
         },
          index: '18B136E08EF50F0DEE8521EA22D16A950CD8B6DDF5F6E07C35F7FDDBBB09718D',
          owner_funds: '0.8095132334507441',
          quality: '332',
          taker_gets_funded: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0.8078974385735969'
         },
          taker_pays_funded: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '268.2219496064341'
         }
       }
     ]
   }
 });
};

module.exports.requestBookOffersAsksResponse = function(request, options={}) {
  _.defaults(options, {
    pays: {
      currency: 'USD',
      issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
    },
    gets: {
      currency: 'BTC',
      issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B'
    }
  });

  return JSON.stringify({
    id: request.id,
    status: 'success',
    type: 'response',
    result: {
      ledger_current_index: 10714274,
      offers: [
        {
          Account: 'rwBYyfufTzk77zUSKEu4MvixfarC35av1J',
          BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC98570B9980E49C7DE8',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000008',
          PreviousTxnID: '92DBA0BE18B331AC61FB277211477A255D3B5EA9C5FE689171DE689FB45FE18A',
          PreviousTxnLgrSeq: 10714030,
          Sequence: 386940,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0.2849323720855092'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '93.030522464522'
         },
          index: '8092033091034D94219BC1131AF7A6B469D790D81831CB479AB6F67A32BE4E13',
          owner_funds: '31.77682120227525',
          quality: '326.5003614141928'
       },
        {
          Account: 'rwjsRktX1eguUr1pHTffyHnC4uyrvX58V1',
          BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC98570BBF1EEFA2FB0A',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000000',
          PreviousTxnID: 'C6BDA152363E3CFE18688A6830B49F3DB2B05976110B5908EA4EB66D93DEEB1F',
          PreviousTxnLgrSeq: 10714031,
          Sequence: 207855,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0.00302447007930511'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '1'
         },
          index: '8DB3520FF9CB16A0EA955056C49115F8CFB03A587D0A4AFC844F1D220EFCE0B9',
          owner_funds: '0.0670537912615556',
          quality: '330.6364334177034'
       },
        {
          Account: 'raudnGKfTK23YKfnS7ixejHrqGERTYNFXk',
          BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC98570BC3A506FC016F',
          BookNode: '0000000000000000',
          Expiration: 472785283,
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '00000000000008F0',
          PreviousTxnID: '77E763F1D02F58965CD1AD94F557B37A582FAC7760B71F391B856959836C2F7B',
          PreviousTxnLgrSeq: 10713576,
          Sequence: 110103,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0.3'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '99.34014894048333'
         },
          index: '9ECDFD31B28643FD3A54658398C5715D6DAD574F83F04529CB24765770F9084D',
          owner_funds: '4.021116654525635',
          quality: '331.1338298016111'
       },
        {
          Account: 'rPyYxUGK8L4dgEvjPs3aRc1B1jEiLr3Hx5',
          BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC98570BCB85BCA78000',
          BookNode: '0000000000000000',
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000000',
          PreviousTxnID: 'D22993C68C94ACE3F2FCE4A334EBEA98CC46DCA92886C12B5E5B4780B5E17D4E',
          PreviousTxnLgrSeq: 10711938,
          Sequence: 392,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0.8095'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '268.754'
         },
          index: '18B136E08EF50F0DEE8521EA22D16A950CD8B6DDF5F6E07C35F7FDDBBB09718D',
          owner_funds: '0.8095132334507441',
          quality: '332',
          taker_gets_funded: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0.8078974385735969'
         },
          taker_pays_funded: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '268.2219496064341'
         }
       },
        {
          Account: 'raudnGKfTK23YKfnS7ixejHrqGERTYNFXk',
          BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC98570C00450D461510',
          BookNode: '0000000000000000',
          Expiration: 472785284,
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '00000000000008F0',
          PreviousTxnID: '1F4D9D859D9AABA888C0708A572B38919A3AEF2C8C1F5A13F58F44C92E5FF3FB',
          PreviousTxnLgrSeq: 10713576,
          Sequence: 110105,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0.4499999999999999'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '152.0098333185607'
         },
          index: '9F380E0B39E2AF8AA9608C3E39A5A8628E6D0F44385C6D12BE06F4FEC8D83351',
          quality: '337.7996295968016'
       },
        {
          Account: 'rDbsCJr5m8gHDCNEHCZtFxcXHsD4S9jH83',
          BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC98570C560B764D760C',
          BookNode: '0000000000000000',
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000001',
          PreviousTxnID: '9A0B6B76F0D86614F965A2FFCC8859D8607F4E424351D4CFE2FBE24510F93F25',
          PreviousTxnLgrSeq: 10708382,
          Sequence: 110061,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0.003768001830745216'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '1.308365894430151'
         },
          index: 'B971769686CE1B9139502770158A4E7C011CFF8E865E5AAE5428E23AAA0E146D',
          owner_funds: '0.2229210189326514',
          quality: '347.2306949944844'
       },
        {
          Account: 'rDVBvAQScXrGRGnzrxRrcJPeNLeLeUTAqE',
          BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC98570C87DF25DC4FC6',
          BookNode: '0000000000000000',
          Expiration: 472783298,
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '00000000000003D2',
          PreviousTxnID: 'E5F9A10F29A4BB3634D5A84FC96931E17267B58E0D2D5ADE24FFB751E52ADB9E',
          PreviousTxnLgrSeq: 10713533,
          Sequence: 35788,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0.5'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '176.3546101589987'
         },
          index: 'D2CB71038AD0ECAF4B5FF0A953AD1257225D0071E6F3AF9ADE67F05590B45C6E',
          owner_funds: '6.617688680663627',
          quality: '352.7092203179974'
       },
        {
          Account: 'rN6jbxx4H6NxcnmkzBxQnbCWLECNKrgSSf',
          BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC98570CC0B8E0E2C000',
          BookNode: '0000000000000000',
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000000',
          PreviousTxnID: '2E16ACFEAC2306E3B3483D445787F3496FACF9504F7A5E909620C1A73E2EDE54',
          PreviousTxnLgrSeq: 10558020,
          Sequence: 491,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0.5'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '179.48'
         },
          index: 'DA853913C8013C9471957349EDAEE4DF4846833B8CCB92008E2A8994E37BEF0D',
          owner_funds: '0.5',
          quality: '358.96',
          taker_gets_funded: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0.499001996007984'
         },
          taker_pays_funded: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '179.1217564870259'
         }
       },
        {
          Account: 'rDVBvAQScXrGRGnzrxRrcJPeNLeLeUTAqE',
          BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC98570CD2F24C9C145D',
          BookNode: '0000000000000000',
          Expiration: 472783299,
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '00000000000003D2',
          PreviousTxnID: 'B1B12E47043B4260223A2C4240D19E93526B55B1DB38DEED335DACE7C04FEB23',
          PreviousTxnLgrSeq: 10713534,
          Sequence: 35789,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0.8'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '288.7710263794967'
         },
          index: 'B89AD580E908F7337CCBB47A0BAAC6417EF13AC3465E34E8B7DD3BED016EA833',
          quality: '360.9637829743709'
       },
        {
          Account: 'rUeCeioKJkbYhv4mRGuAbZpPcqkMCoYq6N',
          BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC98570D0069F50EA028',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000012',
          PreviousTxnID: 'F0E8ABF07F83DF0B5EF5B417E8E29A45A5503BA8F26FBC86447CC6B1FAD6A1C4',
          PreviousTxnLgrSeq: 10447672,
          Sequence: 5255,
          TakerGets: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0.5'
         },
          TakerPays: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '182.9814890090516'
         },
          index: 'D652DCE4B19C6CB43912651D3A975371D3B2A16A034EDF07BC11BF721AEF94A4',
          owner_funds: '0.225891986027944',
          quality: '365.9629780181032',
          taker_gets_funded: {
            currency: options.gets.currency,
            issuer: options.gets.issuer,
            value: '0.2254411038203033'
         },
          taker_pays_funded: {
            currency: options.pays.currency,
            issuer: options.pays.issuer,
            value: '82.50309772176658'
         }
       }
     ],
      validated: false
   }
 });
};

module.exports.requestBookOffersXRPBaseResponse = function(request) {
  return JSON.stringify({
    id: request.id,
    status: 'success',
    type: 'response',
    result: {
      ledger_index: request.ledger_index,
      offers: [
        {
          Account: 'rEiUs9rEiGHmpaprkYDNyXnJYg4ANxWLy9',
          BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C10FB4C37E64D39',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000007',
          PreviousTxnID: 'C4CF947D0C4CCFC667B60ACA552C9381BD4901800297C1DCBA9E162B56FE3097',
          PreviousTxnLgrSeq: 11004060,
          Sequence: 32667,
          TakerGets: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '577.9185501389138'
         },
          TakerPays: '27623954214',
          index: 'B1330CDE9D818DBAF27DF16B9474880710FBC57F309F2A9B7D6AC9C4EBB0C722',
          owner_funds: '577.9127710112036',
          quality: '47799044.01296697',
          taker_gets_funded: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '576.7592525061912'
         },
          taker_pays_funded: '27568540895'
       },
        {
          Account: 'rEiUs9rEiGHmpaprkYDNyXnJYg4ANxWLy9',
          BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C10FB5758F3ACDC',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000007',
          PreviousTxnID: 'BAA6974BC4E267FA53A91A7C820DE5E064FE2329763E42B712F0E0A5F6ABA0C9',
          PreviousTxnLgrSeq: 11004026,
          Sequence: 32661,
          TakerGets: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '578.129773249599'
         },
          TakerPays: '27634326809',
          index: 'D5B3EB16FD23C03716C1ACDE274702D61EFD6807F15284A95C4CDF34375CAF71',
          quality: '47799522.00461532',
          taker_gets_funded: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '0'
         },
          taker_pays_funded: '0'
       },
        {
          Account: 'rsvZ4ucGpMvfSYFQXB4nFaQhxiW5CUy2zx',
          BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C10FB627A06C000',
          BookNode: '0000000000000000',
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000002',
          PreviousTxnID: '6221CBEC5E06E604B5AD32979D4C04CD3EA24404B6E07EC3508E708CC6FC1A9D',
          PreviousTxnLgrSeq: 11003996,
          Sequence: 549,
          TakerGets: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '265.0254187774191'
         },
          TakerPays: '12668215016',
          index: 'A9AC351832B9A17FDA35650B6EE32C0A48F6AC661730E9855CC47498C171860C',
          owner_funds: '2676.502797501436',
          quality: '47800000'
       },
        {
          Account: 'rfCFLzNJYvvnoGHWQYACmJpTgkLUaugLEw',
          BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C110D996CF89F0E',
          BookNode: '0000000000000000',
          Expiration: 474062867,
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '00000000000000E1',
          PreviousTxnID: '80CFB17188F02FF19759CC19E6E543ACF3F7299C11746FCDD8D7D3BBD18FBC5E',
          PreviousTxnLgrSeq: 11004047,
          Sequence: 2665862,
          TakerGets: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '193'
         },
          TakerPays: '9264052522',
          index: '67C79EAA9F4EB638E2FC8F569C29E9E02F79F38BB136B66FD13857EB60432913',
          owner_funds: '3962.913768867934',
          quality: '48000272.13471502'
       },
        {
          Account: 'rM3X3QSr8icjTGpaF52dozhbT2BZSXJQYM',
          BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C110F696023CF97',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000005302',
          PreviousTxnID: '4DAC7F491E106C4BF2292C387144AB08D758B3DE04A1698BBD97468C893A375B',
          PreviousTxnLgrSeq: 11003934,
          Sequence: 1425976,
          TakerGets: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '161.8304886'
         },
          TakerPays: '7771132207',
          index: 'A9696EDF0D7AC89ACBAF62E3CEDCD14DE82B441046CC748FE92A1DEB90D40A4A',
          owner_funds: '2673.609970934654',
          quality: '48020198.63023511'
       },
        {
          Account: 'r4rCiFc9jpMeCpKioVJUMbT1hU4kj3XiSt',
          BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C11107D2C579CB9',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000000',
          PreviousTxnID: '9559B82392D110D543FA47670A0619A423078FABD8DBD69B6620B83CBC851BE2',
          PreviousTxnLgrSeq: 11003872,
          Sequence: 44405,
          TakerGets: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '605.341293717861'
         },
          TakerPays: '29075779685',
          index: 'F38F7F427823927F3870AB66E9C01529DDA7567CE62F9687992729B8F14E7937',
          owner_funds: '637.5308256246498',
          quality: '48032044.04976825'
       },
        {
          Account: 'rNEib8Z73zSTYTi1WqzU4b1BQMXxnpYg1s',
          BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C11108A8F4D49EA',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000001',
          PreviousTxnID: '1C41470EFDF9D83252C6EA702B7B8D8824A560D63B75880D652B1898D8519B98',
          PreviousTxnLgrSeq: 11004058,
          Sequence: 781823,
          TakerGets: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '166.647580164238'
         },
          TakerPays: '8004519725',
          index: '042EDAF04F4C0709831439DEC15384CA9C5C9926B63FB87D1352BD5FEDD2FC68',
          owner_funds: '166.6476801642377',
          quality: '48032618.99819498',
          taker_gets_funded: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '166.3150500641094'
         },
          taker_pays_funded: '7988547433'
       },
        {
          Account: 'rPCFVxAqP2XdaPmih1ZSjmCPNxoyMiy2ne',
          BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C11108A97DE552A',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000001',
          PreviousTxnID: '053C4A7116D258C2D83128B0552ADA2FCD3C50058953495B382114DED5D03CD1',
          PreviousTxnLgrSeq: 11003869,
          Sequence: 50020,
          TakerGets: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '792.5367174829754'
         },
          TakerPays: '38067615332',
          index: 'EFF326D799C5D722F4367250DC3C9E3DADB300D4E96D634A58E1F4B534F754C7',
          owner_funds: '816.6776190772376',
          quality: '48032620.43542826'
       },
        {
          Account: 'rEiUs9rEiGHmpaprkYDNyXnJYg4ANxWLy9',
          BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C111177AF892263',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000007',
          PreviousTxnID: 'D7E587874A4ACB0F2F5557416E8834EA3A9A9DCCF493A139DFC1DF1AA21FA24C',
          PreviousTxnLgrSeq: 11003728,
          Sequence: 32647,
          TakerGets: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '732.679143498934'
         },
          TakerPays: '35199960104',
          index: '3B2748D2270588F2A180CA2C6B7262B3D95F37BF5EE052600059F23BFFD4ED82',
          quality: '48042803.47861603',
          taker_gets_funded: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '0'
         },
          taker_pays_funded: '0'
       },
        {
          Account: 'rEiUs9rEiGHmpaprkYDNyXnJYg4ANxWLy9',
          BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C111182DF1CF82E',
          BookNode: '0000000000000000',
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000007',
          PreviousTxnID: 'CD503C7524F4C16369C671D1E3419B8C7AA45D4D9049137B700C1F83F4B2A6ED',
          PreviousTxnLgrSeq: 11003716,
          Sequence: 32645,
          TakerGets: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '732.679143498934'
         },
          TakerPays: '35200312104',
          index: 'C307AAACE73B101EA03D33C4A9FADECA19439F49F0AAF080FE37FA676B69F6D5',
          quality: '48043283.90719534',
          taker_gets_funded: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '0'
         },
          taker_pays_funded: '0'
       }
     ],
      validated: true
   }
 });
};

module.exports.requestBookOffersXRPCounterResponse = function(request) {
  return JSON.stringify({
    id: request.id,
    status: 'success',
    type: 'response',
    result: {
      ledger_index: request.ledger_index,
      offers: [
        {
          Account: 'rDhvfvsyBBTe8VFRp9q9hTmuUH91szQDyo',
          BookDirectory: 'DFA3B6DDAB58C7E8E5D944E736DA4B7046C30E4F460FD9DE4D0773EDCBC36C00',
          BookNode: '0000000000000000',
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000000',
          PreviousTxnID: 'CE8CF4B56D56B8625E97A0A885750228DA04FB3957F55DB812F571E82DBF409D',
          PreviousTxnLgrSeq: 11003859,
          Sequence: 554,
          TakerGets: '1000000000',
          TakerPays: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '20.9779'
         },
          index: 'CE14A7A75F12FDD166FDFBE446CF737CE0D467F6F11299E69DA10EDFD9C984EB',
          owner_funds: '1037828768',
          quality: '0.0000000209779'
       },
        {
          Account: 'rLVCrkavabdvHiNtcMedN3BAmz3AUc2L5j',
          BookDirectory: 'DFA3B6DDAB58C7E8E5D944E736DA4B7046C30E4F460FD9DE4D07741EB0BD2000',
          BookNode: '0000000000000000',
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '000000000000000A',
          PreviousTxnID: '3F98A2AC970BCB077BFB6E6A99D1395C878DBD869F8327FE3BA226DE50229898',
          PreviousTxnLgrSeq: 10997095,
          Sequence: 7344,
          TakerGets: '70000000000',
          TakerPays: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '1468.6'
         },
          index: '9D7D345AC880071453B18F78BB2CDD8DB98B38025F4C6DC8A9CDA7ECDD7229C1',
          owner_funds: '189998700393',
          quality: '0.00000002098'
       },
        {
          Account: 'rL5916QJwSMnUqcCv9savsXA7Xtq83fhzS',
          BookDirectory: 'DFA3B6DDAB58C7E8E5D944E736DA4B7046C30E4F460FD9DE4D0775F05A074000',
          BookNode: '0000000000000000',
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000000',
          PreviousTxnID: 'C8BA89CE93BCC203225337086752AB98AD87D201CFE8B8F768D8B33A95A442C9',
          PreviousTxnLgrSeq: 10996282,
          Sequence: 243,
          TakerGets: '100000000000',
          TakerPays: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '2100'
         },
          index: '307CE6343140915231E11E0FE993D2A256DEC18092A2B546EFA0B4214139FAFC',
          owner_funds: '663185088622',
          quality: '0.000000021'
       },
        {
          Account: 'rGPmoJKzmocGgJoWUmU4KYxig2RUC7cESo',
          BookDirectory: 'DFA3B6DDAB58C7E8E5D944E736DA4B7046C30E4F460FD9DE4D0777C203516000',
          BookNode: '0000000000000000',
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000000',
          PreviousTxnID: '3DE17110F54AD94B4F853ADA985AB02EE9E4B6382E9912E0128ED0D0DCAF71D2',
          PreviousTxnLgrSeq: 10997799,
          Sequence: 91,
          TakerGets: '58200000000',
          TakerPays: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '1223.364'
         },
          index: '8BFFF54B164E52C4B66EE193829D589EA03F0FAD86A585EB3208C3D2FDEE2CAF',
          owner_funds: '58199748000',
          quality: '0.00000002102',
          taker_gets_funded: '58199748000',
          taker_pays_funded: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '1223.35870296'
         }
       },
        {
          Account: 'rfCFLzNJYvvnoGHWQYACmJpTgkLUaugLEw',
          BookDirectory: 'DFA3B6DDAB58C7E8E5D944E736DA4B7046C30E4F460FD9DE4D077C885DE9E684',
          BookNode: '0000000000000000',
          Expiration: 474062867,
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '00000000000000E1',
          PreviousTxnID: '3E6B6CB8518CC6DF19E8EA0D38D0268ECE35DCCB59DE25DC5A340D4DB68312F8',
          PreviousTxnLgrSeq: 11004047,
          Sequence: 2665861,
          TakerGets: '8589393882',
          TakerPays: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '181'
         },
          index: 'B2247E0118DAF88318EBB495A03F54C115FC9420AF85499D95D208912B059A86',
          owner_funds: '412756265349',
          quality: '0.0000000210724996998106'
       },
        {
          Account: 'rn7Dk7YcNRmUb9q9WUVX1oh9Kp1Dkuy9xE',
          BookDirectory: 'DFA3B6DDAB58C7E8E5D944E736DA4B7046C30E4F460FD9DE4D077CB4EB7E6D1E',
          BookNode: '0000000000000000',
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000009',
          PreviousTxnID: '2AF95FB960B7E4E0E9E88BCBF4465BD37DCC7D0B2D8C640F6B4384DDAB77E75E',
          PreviousTxnLgrSeq: 10999181,
          Sequence: 881318,
          TakerGets: '1983226007',
          TakerPays: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '41.79532441712259'
         },
          index: 'AC3E2571C15C4A405D19DD0665B798AAACE843D87849544237DA7B29541990AB',
          owner_funds: '0',
          quality: '0.00000002107441323863326',
          taker_gets_funded: '0',
          taker_pays_funded: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '0'
         }
       },
        {
          Account: 'rLVCrkavabdvHiNtcMedN3BAmz3AUc2L5j',
          BookDirectory: 'DFA3B6DDAB58C7E8E5D944E736DA4B7046C30E4F460FD9DE4D077F08A879E000',
          BookNode: '0000000000000000',
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '000000000000000A',
          PreviousTxnID: '68D5EEA742C3EFD3E8CB3B998542042885C2D7E4BFAF47C15DF1681AD9D1E42A',
          PreviousTxnLgrSeq: 10995569,
          Sequence: 7335,
          TakerGets: '60000000000',
          TakerPays: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '1266'
         },
          index: '4ED8112E78F86DB75E5A14A6C2EE45F3EA9CBC5644EAFA81F970F688F0CC04D7',
          quality: '0.0000000211'
       },
        {
          Account: 'rN24WWiyC6q1yWmm6b3Z6yMycohvnutLUQ',
          BookDirectory: 'DFA3B6DDAB58C7E8E5D944E736DA4B7046C30E4F460FD9DE4D077F08A879E000',
          BookNode: '0000000000000000',
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000000',
          PreviousTxnID: '22BDB32176381066833FC64BC1D6D56F033F99695CBC996DAC848C36F2FC800C',
          PreviousTxnLgrSeq: 10996022,
          Sequence: 367,
          TakerGets: '8000000000',
          TakerPays: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '168.8'
         },
          index: '7B1F18E506BA7B06C18B10A4D0F45FA6213F94B5ACA1912B0C4A3C9F899862D5',
          owner_funds: '16601355477',
          quality: '0.0000000211'
       },
        {
          Account: 'rfCFLzNJYvvnoGHWQYACmJpTgkLUaugLEw',
          BookDirectory: 'DFA3B6DDAB58C7E8E5D944E736DA4B7046C30E4F460FD9DE4D0782A08429CE0B',
          BookNode: '0000000000000000',
          Expiration: 474062643,
          Flags: 0,
          LedgerEntryType: 'Offer',
          OwnerNode: '00000000000000E1',
          PreviousTxnID: 'A737529DC88EE6EFF684729BFBEB1CD89FF186A20041D6237EB1B36A192C4DF3',
          PreviousTxnLgrSeq: 11004000,
          Sequence: 2665799,
          TakerGets: '85621672636',
          TakerPays: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '1810'
         },
          index: 'C63B614E60EB46FAA4F9F5E3B6D5535E18F909F936788514280F59EDB6C899BD',
          quality: '0.00000002113950760685067'
       },
        {
          Account: 'rHRC9cBUYwEnrDZce6SkAkDTo8P9G1un3U',
          BookDirectory: 'DFA3B6DDAB58C7E8E5D944E736DA4B7046C30E4F460FD9DE4D078394CFB33000',
          BookNode: '0000000000000000',
          Flags: 131072,
          LedgerEntryType: 'Offer',
          OwnerNode: '0000000000000000',
          PreviousTxnID: 'B08E26494F49ACCDFA22151369E2171558E5D62BA22DCFFD001DF2C45E0727DB',
          PreviousTxnLgrSeq: 10984137,
          Sequence: 17,
          TakerGets: '100752658247',
          TakerPays: {
            currency: 'USD',
            issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
            value: '2130.918721932529'
         },
          index: '72F31EC2BDE26A721CC78EB8D42B2BC4A3E36DCF6E7D4B73C53D4A40F4728A88',
          owner_funds: '4517636158733',
          quality: '0.00000002115'
       }
     ],
      validated: true
   }
 });
};
