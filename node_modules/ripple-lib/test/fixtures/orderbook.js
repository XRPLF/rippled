/*eslint-disable max-len, no-param-reassign*/

'use strict';

const _ = require('lodash');
const addresses = require('./addresses');
const Meta = require('ripple-lib').Meta;

module.exports.FIAT_BALANCE = '10';
module.exports.NATIVE_BALANCE = '55';
module.exports.NATIVE_BALANCE_PREVIOUS = '100';

module.exports.TAKER_GETS = '19.84580331';
module.exports.TAKER_GETS_FINAL = '18.84580331';
module.exports.TAKER_PAYS = '3878342440';
module.exports.TAKER_PAYS_FINAL = '3078342440';
module.exports.OTHER_TAKER_GETS = '4.9656112525';
module.exports.OTHER_TAKER_GETS_FINAL = '3.9656112525';
module.exports.OTHER_TAKER_PAYS = '972251352';
module.exports.OTHER_TAKER_PAYS_FINAL = '902251352';

module.exports.LEDGER_INDEX = '06AFB03237286C1566CD649CFD5388C2C1F5BEFC5C3302A1962682803A9946FA';
module.exports.OTHER_LEDGER_INDEX = 'D3338DA77BA23122FB5647B74B53636AB54BE246D4B21707C9D6887DEB334252';

module.exports.TRANSFER_RATE = 1002000000;

module.exports.fiatOffers = function(options) {
  options = options || {};
  _.defaults(options, {
    account_funds: '318.3643710638508',
    other_account_funds: '235.0194163432668'
  });

  return [
    {
      Account: addresses.ACCOUNT,
      BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5D06F15E821839FB',
      BookNode: '0000000000000000',
      Flags: 0,
      LedgerEntryType: 'Offer',
      OwnerNode: '0000000000001897',
      PreviousTxnID: '11BA57676711A42C2FC2191EAEE98023B04627DFA84926B0C8E9D61A9CAF13AD',
      PreviousTxnLgrSeq: 8265601,
      Sequence: 531927,
      TakerGets: {
        currency: 'USD',
        issuer: addresses.ISSUER,
        value: module.exports.TAKER_GETS
      },
      taker_gets_funded: '100',
      is_fully_funded: true,
      TakerPays: module.exports.TAKER_PAYS,
      index: module.exports.LEDGER_INDEX,
      owner_funds: options.account_funds,
      quality: '195423807.2109563'
    },
    {
      Account: addresses.ACCOUNT,
      BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5D06F4C3362FE1D0',
      BookNode: '0000000000000000',
      Flags: 0,
      LedgerEntryType: 'Offer',
      OwnerNode: '00000000000063CC',
      PreviousTxnID: 'CD77500EF28984BFC123E8A257C10E44FF486EA8FC43E1356C42BD6DB853A602',
      PreviousTxnLgrSeq: 8265523,
      Sequence: 1139002,
      TakerGets: {
        currency: 'USD',
        issuer: addresses.ISSUER,
        value: '4.9656112525'
      },
      taker_gets_funded: '100',
      is_fully_funded: true,
      TakerPays: '972251352',
      index: 'X2K98DB77BA23122FB5647B74B53636AB54BE246D4B21707C9D6887DEB334252',
      owner_funds: options.account_funds,
      quality: '195796912.5171664'
    },
    {
      Account: addresses.OTHER_ACCOUNT,
      BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5D06F4C3362FE1D0',
      BookNode: '0000000000000000',
      Flags: 0,
      LedgerEntryType: 'Offer',
      OwnerNode: '00000000000063CC',
      PreviousTxnID: 'CD77500EF28984BFC123E8A257C10E44FF486EA8FC43E1356C42BD6DB853A602',
      PreviousTxnLgrSeq: 8265523,
      Sequence: 1139002,
      TakerGets: {
        currency: 'USD',
        issuer: addresses.ISSUER,
        value: module.exports.OTHER_TAKER_GETS
      },
      taker_gets_funded: '100',
      is_fully_funded: true,
      TakerPays: module.exports.OTHER_TAKER_PAYS,
      index: module.exports.OTHER_LEDGER_INDEX,
      owner_funds: options.other_account_funds,
      quality: '195796912.5171664'
    }
  ];
};

module.exports.NATIVE_OFFERS = [
  {
    Account: addresses.ACCOUNT,
    BookDirectory: 'DFA3B6DDAB58C7E8E5D944E736DA4B7046C30E4F460FD9DE4C124AF94ED1781B',
    BookNode: '0000000000000000',
    Flags: 0,
    LedgerEntryType: 'Offer',
    OwnerNode: '00000000000063CA',
    PreviousTxnID: '51C64E0B300E9C0E877BA3E79B4ED1DBD5FDDCE58FA1A8FDA5F8DDF139787A24',
    PreviousTxnLgrSeq: 8265275,
    Sequence: 1138918,
    TakerGets: '50',
    taker_gets_funded: '100',
    is_fully_funded: true,
    TakerPays: {
      currency: 'USD',
      issuer: addresses.ISSUER,
      value: '5'
    },
    index: 'DC003E09AD1306FBBD1957C955EE668E429CC85B0EC0EC17297F6676E6108DE7',
    owner_funds: '162110617177',
    quality: '0.000000005148984210454555'
  },
  {
    Account: addresses.ACCOUNT,
    BookDirectory: 'DFA3B6DDAB58C7E8E5D944E736DA4B7046C30E4F460FD9DE4C124B054BAD1D79',
    BookNode: '0000000000000000',
    Flags: 0,
    LedgerEntryType: 'Offer',
    OwnerNode: '0000000000001896',
    PreviousTxnID: '9B21C7A4B66DC1CD5FC9D85C821C4CAA8F80E437582BAD11E88A1E9E6C7AA59C',
    PreviousTxnLgrSeq: 8265118,
    Sequence: 531856,
    TakerGets: '10',
    taker_gets_funded: '100',
    is_fully_funded: true,
    TakerPays: {
      currency: 'USD',
      issuer: addresses.ISSUER,
      value: '20'
    },
    index: '7AC0458676A54E99FAA5ED0A56CD0CB814D3DEFE1C7874F0BB39875D60668E41',
    owner_funds: '430527438338',
    quality: '0.000000005149035697347961'
  },
  {
    Account: addresses.OTHER_ACCOUNT,
    BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5D06F4C3362FE1D0',
    BookNode: '0000000000000000',
    Flags: 0,
    LedgerEntryType: 'Offer',
    OwnerNode: '00000000000063CC',
    PreviousTxnID: 'CD77500EF28984BFC123E8A257C10E44FF486EA8FC43E1356C42BD6DB853A602',
    PreviousTxnLgrSeq: 8265523,
    Sequence: 1139002,
    TakerGets: '972251352',
    TakerPays: {
      currency: 'USD',
      issuer: addresses.ISSUER,
      value: '4.9656112525'
    },
    index: 'D3338DA77BA23122FB5647B74B53636AB54BE246D4B21707C9D6887DEB334252',
    owner_funds: '235.0194163432668',
    quality: '195796912.5171664'
  }
];

module.exports.REQUEST_OFFERS = [
  {
    Account: addresses.ACCOUNT,
    BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC985711A3A4254F5000',
    BookNode: '0000000000000000',
    Flags: 131072,
    LedgerEntryType: 'Offer',
    OwnerNode: '0000000000000000',
    PreviousTxnID: '9BB337CC8B34DC8D1A3FFF468556C8BA70977C37F7436439D8DA19610F214AD1',
    PreviousTxnLgrSeq: 8342933,
    Sequence: 195,
    TakerGets: {
      currency: 'BTC',
      issuer: addresses.ISSUER,
      value: '0.1129232560043778'
    },
    TakerPays: {
      currency: 'USD',
      issuer: addresses.ISSUER,
      value: '56.06639660617357'
    },
    index: 'B6BC3B0F87976370EE11F5575593FE63AA5DC1D602830DC96F04B2D597F044BF',
    owner_funds: '0.1129267125000245',
    quality: '496.4999999999999'
  },
  {
    Account: addresses.OTHER_ACCOUNT,
    BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC985711B6D8C62EF414',
    BookNode: '0000000000000000',
    Expiration: 461498565,
    Flags: 131072,
    LedgerEntryType: 'Offer',
    OwnerNode: '0000000000000144',
    PreviousTxnID: 'C8296B9CCA6DC594C7CD271C5D8FD11FEE380021A07768B25935642CDB37048A',
    PreviousTxnLgrSeq: 8342469,
    Sequence: 29354,
    TakerGets: {
      currency: 'BTC',
      issuer: addresses.ISSUER,
      value: '0.2'
    },
    TakerPays: {
      currency: 'USD',
      issuer: addresses.ISSUER,
      value: '99.72233516476456'
    },
    index: 'A437D85DF80D250F79308F2B613CF5391C7CF8EE9099BC4E553942651CD9FA86',
    owner_funds: '0.950363009783092',
    quality: '498.6116758238228'
  },
  {
    Account: addresses.THIRD_ACCOUNT,
    BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC985711B6D8C62EF414',
    BookNode: '0000000000000000',
    Expiration: 461498565,
    Flags: 131072,
    LedgerEntryType: 'Offer',
    OwnerNode: '0000000000000144',
    PreviousTxnID: 'C8296B9CCA6DC594C7CD271C5D8FD11FEE380021A07768B25935642CDB37048A',
    PreviousTxnLgrSeq: 8342469,
    Sequence: 29356,
    TakerGets: {
      currency: 'BTC',
      issuer: addresses.ISSUER,
      value: '0.5'
    },
    TakerPays: {
      currency: 'USD',
      issuer: addresses.ISSUER,
      value: '99.72233516476456'
    },
    index: 'A437D85DF80D250F79308F2B613CF5391C7CF8EE9099BC4E553942651CD9FA86',
    owner_funds: '0.950363009783092',
    quality: '498.6116758238228'
  },
  {
    Account: addresses.THIRD_ACCOUNT,
    BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC985711B6D8C62EF414',
    BookNode: '0000000000000000',
    Expiration: 461498565,
    Flags: 131078,
    LedgerEntryType: 'Offer',
    OwnerNode: '0000000000000144',
    PreviousTxnID: 'C8296B9CCA6DC594C7CD271C5D8FD11FEE380021A07768B25935642CDB37048A',
    PreviousTxnLgrSeq: 8342469,
    Sequence: 29354,
    TakerGets: {
      currency: 'BTC',
      issuer: addresses.ISSUER,
      value: '0.5'
    },
    TakerPays: {
      currency: 'USD',
      issuer: addresses.ISSUER,
      value: '99.72233516476456'
    },
    index: 'A437D85DF80D250F79308F2B613CF5391C7CF8EE9099BC4E553942651CD9FA86',
    owner_funds: '0.950363009783092',
    quality: '199.4446703295291'
  }
];

module.exports.REQUEST_OFFERS_NATIVE = [
  {
    Account: addresses.ACCOUNT,
    BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC985711A3A4254F5000',
    BookNode: '0000000000000000',
    Flags: 131072,
    LedgerEntryType: 'Offer',
    OwnerNode: '0000000000000000',
    Sequence: 195,
    TakerGets: '1000',
    TakerPays: {
      currency: 'USD',
      issuer: addresses.ISSUER,
      value: '56.06639660617357'
    },
    index: 'B6BC3B0F87976370EE11F5575593FE63AA5DC1D602830DC96F04B2D597F044BF',
    owner_funds: '600',
    quality: '.0560663966061735'
  },
  {
    Account: addresses.OTHER_ACCOUNT,
    BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC985711B6D8C62EF414',
    BookNode: '0000000000000000',
    Expiration: 461498565,
    Flags: 131072,
    LedgerEntryType: 'Offer',
    OwnerNode: '0000000000000144',
    Sequence: 29354,
    TakerGets: '2000',
    TakerPays: {
      currency: 'USD',
      issuer: addresses.ISSUER,
      value: '99.72233516476456'
    },
    index: 'A437D85DF80D250F79308F2B613CF5391C7CF8EE9099BC4E553942651CD9FA86',
    owner_funds: '4000',
    quality: '0.049861167582382'
  },
  {
    Account: addresses.THIRD_ACCOUNT,
    BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC985711B6D8C62EF414',
    BookNode: '0000000000000000',
    Expiration: 461498565,
    Flags: 131072,
    LedgerEntryType: 'Offer',
    OwnerNode: '0000000000000144',
    Sequence: 29356,
    TakerGets: '2000',
    TakerPays: {
      currency: 'USD',
      issuer: addresses.ISSUER,
      value: '99.72233516476456'
    },
    index: 'A437D85DF80D250F79308F2B613CF5391C7CF8EE9099BC4E553942651CD9FA86',
    owner_funds: '3900',
    quality: '0.049861167582382'
  },
  {
    Account: addresses.THIRD_ACCOUNT,
    BookDirectory: '6EAB7C172DEFA430DBFAD120FDC373B5F5AF8B191649EC985711B6D8C62EF414',
    BookNode: '0000000000000000',
    Expiration: 461498565,
    Flags: 131078,
    LedgerEntryType: 'Offer',
    OwnerNode: '0000000000000144',
    PreviousTxnID: 'C8296B9CCA6DC594C7CD271C5D8FD11FEE380021A07768B25935642CDB37048A',
    PreviousTxnLgrSeq: 8342469,
    Sequence: 29354,
    TakerGets: '2000',
    TakerPays: {
      currency: 'USD',
      issuer: addresses.ISSUER,
      value: '99.72233516476456'
    },
    index: 'A437D85DF80D250F79308F2B613CF5391C7CF8EE9099BC4E553942651CD9FA86',
    quality: '0.049861167582382'
  }
];

module.exports.QUALITY_OFFERS = [
  {
    Account: addresses.ACCOUNT,
    BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C1AFE1EE71A605F',
    BookNode: '0000000000000000',
    Flags: 0,
    LedgerEntryType: 'Offer',
    OwnerNode: '0000000000000009',
    PreviousTxnID: 'BCA728C17DBA10F100C41D4EF8B37318F33BC6156E94DB16703D2A1EE43DCCE6',
    PreviousTxnLgrSeq: 11929146,
    Sequence: 668643,
    TakerGets: {
      currency: 'USD',
      issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
      value: '301.3426005599325'
    },
    TakerPays: '22895281765',
    index: '79B34D7DF703580B86099EFD6B2E419AA39A585A50C82A3F9B446721B7C1490C',
    owner_funds: '5910.437716613066',
    quality: '75977580.74216543'
  }
];

// This fixture is to exercise a bug where taker_pays_funded = taker_gets_funded * quality
// has decimal amounts.
module.exports.DECIMAL_TAKER_PAYS_FUNDED_OFFERS = [
  {
    Account: addresses.ACCOUNT,
    BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5D0689673FA9094A',
    BookNode: '0000000000000000',
    Flags: 0,
    LedgerEntryType: 'Offer',
    OwnerNode: '0000000000000006',
    PreviousTxnID: 'C1BB04CE39E30BF5982B7660793723E9B3A832F5B458DB1C5938F4737E0E9ABF',
    PreviousTxnLgrSeq: 11631257,
    Sequence: 2936,
    TakerGets: {
      currency: 'USD',
      issuer: addresses.ISSUER,
      value: '9280.04'
    },
    TakerPays: '1707459061637',
    index: '89D85BBE91E0F419953EB89CE62E194922ED930EE57BE0C62FCC3B22DDB20852',
    owner_funds: '9280.037154029904',
    quality: '183992640.2943306',
    taker_gets_funded: {
      currency: 'USD',
      issuer: addresses.ISSUER,
      value: '9261.514125778347'
    },
    taker_pays_funded: '1704050437125'
  }
];

module.exports.LEG_ONE_OFFERS = [
  {
    Account: addresses.ACCOUNT,
    BookDirectory: 'DFA3B6DDAB58C7E8E5D944E736DA4B7046C30E4F460FD9DE4D043654A0DBD245',
    BookNode: '0000000000000000',
    Flags: 0,
    LedgerEntryType: 'Offer',
    OwnerNode: '0000000000000078',
    PreviousTxnID: '27723DCE3E6DB324DBCE9F0C9110352DBBC04DD6BEFE2A57C4E524FD215144C9',
    PreviousTxnLgrSeq: 12024847,
    Sequence: 14532890,
    TakerGets: '31461561812',
    TakerPays: {
      currency: 'USD',
      issuer: addresses.ISSUER,
      value: '373.019921005'
    },
    index: '7EEE980B0BD43C15504B9A89164D29EF02DBBD3807DA7936F51EA2CE3D0C6324',
    owner_funds: '210586312936',
    quality: '0.00000001185637010756165'
  },
  {
    Account: addresses.OTHER_ACCOUNT,
    BookDirectory: 'DFA3B6DDAB58C7E8E5D944E736DA4B7046C30E4F460FD9DE4D043676B9DEA2FC',
    BookNode: '0000000000000000',
    Flags: 0,
    LedgerEntryType: 'Offer',
    OwnerNode: '0000000000000002',
    PreviousTxnID: '1B36F7DE44C96FBDB50F8F80D24D3FA11454CB837BA4E4D667C92E01AE9225F5',
    PreviousTxnLgrSeq: 12024788,
    Sequence: 244399,
    TakerGets: '25299728855',
    TakerPays: {
      currency: 'USD',
      issuer: addresses.ISSUER,
      value: '300'
    },
    index: '5F8BDA3343CB792FA0DD55740F5827C5E050A287C96FDE4F7DFF548693420744',
    owner_funds: '1291056089559',
    quality: '0.00000001185783459259132'
  },
  {
    Account: addresses.THIRD_ACCOUNT,
    BookDirectory: 'DFA3B6DDAB58C7E8E5D944E736DA4B7046C30E4F460FD9DE4D0437FF40E6F02A',
    BookNode: '0000000000000000',
    Expiration: 478636633,
    Flags: 0,
    LedgerEntryType: 'Offer',
    OwnerNode: '0000000000000165',
    PreviousTxnID: 'D42D81273BDC3ED611ED84DF07EA55E31703F4E05BC70CC12871715FCB58E160',
    PreviousTxnLgrSeq: 12024847,
    Sequence: 3858033,
    TakerGets: '18189943147',
    TakerPays: {
      currency: 'USD',
      issuer: addresses.ISSUER,
      value: '216'
    },
    index: 'FD5E66163DFE67919E64F31D506A8F3E94802E6A0FFEBE7A6FD40A2F1135EDD4',
    owner_funds: '490342145233',
    quality: '0.0000000118746935190737'
  }
];

module.exports.LEG_TWO_OFFERS = [
  {
    Account: addresses.FOURTH_ACCOUNT,
    BookDirectory: 'DA36FDE1B8CE294B214BE4E4C958DAAF9C1F46DE1FCB44115D0A4929E095B160',
    BookNode: '0000000000000000',
    Flags: 0,
    LedgerEntryType: 'Offer',
    OwnerNode: '0000000000000003',
    PreviousTxnID: '97A8D6B2135231363EC1B3B509DF052D481A0045684464948E6DF2C2B9FC1E64',
    PreviousTxnLgrSeq: 12004045,
    Sequence: 384,
    TakerGets: {
      currency: 'EUR',
      issuer: addresses.ISSUER,
      value: '17.07639524223001'
    },
    TakerPays: '4943947661',
    index: '5B00ACF35041983F070EAE2219C274D24A11D6FD6FE4306A4C72E7B769D4F914',
    owner_funds: '36.40299530003982',
    quality: '289519397.75'
  },
  {
    Account: addresses.FOURTH_ACCOUNT,
    BookDirectory: 'DA36FDE1B8CE294B214BE4E4C958DAAF9C1F46DE1FCB44115E12B2D070B5DBE0',
    BookNode: '0000000000000000',
    Flags: 0,
    LedgerEntryType: 'Offer',
    OwnerNode: '0000000000000006',
    PreviousTxnID: '425EBA467DD335602BAFBAB5329B1E7FC1ABB325AA5CD4495A5085860D09F2BE',
    PreviousTxnLgrSeq: 11802828,
    Sequence: 605,
    TakerGets: {
      currency: 'EUR',
      issuer: addresses.ISSUER,
      value: '19.99999999954904'
    },
    TakerPays: '105263157889',
    index: '8715E674302D446EBD520FF11B48A0F64822F4F9266D62544987223CA16EDBB1',
    quality: '5263157894.7',
    taker_gets_funded: {
      currency: 'EUR',
      issuer: 'rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B',
      value: '19.25393938854825'
    },
    taker_pays_funded: '101336523096'
  }
];

module.exports.bookOffersResponse = function(options) {
  options = options || {};
  _.defaults(options, {
    account_funds: '2010.027702881682',
    other_account_funds: '24.06086596039299',
    third_account_funds: '9071.40090264774',
    fourth_account_funds: '7244.053477923128'
  });

  return {
    offers: [
      {
        Account: addresses.ACCOUNT,
        BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C188F5B29EE1C14',
        BookNode: '0000000000000000',
        Flags: 0,
        LedgerEntryType: 'Offer',
        OwnerNode: '0000000000006762',
        PreviousTxnID: '5F08192C82CD3A598D29B51FCCDE29B6709EBCB454A3CD540C32F7A79EE7CB26',
        PreviousTxnLgrSeq: 11558364,
        Sequence: 1689777,
        TakerGets: {
          currency: 'USD',
          issuer: addresses.ISSUER,
          value: '79.39192374'
        },
        TakerPays: '5488380479',
        index: 'D9F821C8687E0D0EDEFF05EBB53CFDC81C5F9C4C354DAACB11F6676B5E14AEF5',
        owner_funds: options.account_funds,
        quality: '69130211.4932226'
      },
      {
        Account: addresses.OTHER_ACCOUNT,
        BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C18949C72B26C2A',
        BookNode: '0000000000000000',
        Flags: 0,
        LedgerEntryType: 'Offer',
        OwnerNode: '0000000000000001',
        PreviousTxnID: '038ED9ACC10211A8F6768729F36B74729CECCD33057837E160131675B272E532',
        PreviousTxnLgrSeq: 11558374,
        Sequence: 931088,
        TakerGets: {
          currency: 'USD',
          issuer: addresses.ISSUER,
          value: '24.060765960393'
        },
        TakerPays: '1664716059',
        index: '8845F212A8B53004A14C8C029FAF51B53266C66B49281A72F6A8F41CD92FDE99',
        owner_funds: options.other_account_funds,
        quality: '69187991.0116049',
        taker_gets_funded: {
          currency: 'USD',
          issuer: addresses.ISSUER,
          value: '24.01284027983332'
        },
        taker_pays_funded: '1661400177'
      },
      {
        Account: addresses.THIRD_ACCOUNT,
        BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C18949C764EA14E',
        BookNode: '0000000000000000',
        Flags: 0,
        LedgerEntryType: 'Offer',
        OwnerNode: '0000000000000009',
        PreviousTxnID: '62B96C0E0D86827BCE59ABDCAD146CC0B09404FE5BC86E712FB6F4E473016C63',
        PreviousTxnLgrSeq: 11558234,
        Sequence: 617872,
        TakerGets: {
          currency: 'USD',
          issuer: addresses.ISSUER,
          value: '712.60995'
        },
        TakerPays: '49304051247',
        index: '9E5C13908F67146AC35A711A17E5EB75771FDDA816C9532891DC90F29A6A4C10',
        owner_funds: options.third_account_funds,
        quality: '69187991.61729358'
      },
      {
        Account: addresses.FOURTH_ACCOUNT,
        BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C18AA2E761B7EE6',
        BookNode: '0000000000000000',
        Flags: 0,
        LedgerEntryType: 'Offer',
        OwnerNode: '0000000000000511',
        PreviousTxnID: 'F18AED5EC1E7529EF03AF23ADA85F7625AA308278BACE1851F336443AA3DAAEA',
        PreviousTxnLgrSeq: 11558336,
        Sequence: 662712,
        TakerGets: {
          currency: 'USD',
          issuer: addresses.ISSUER,
          value: '288.08'
        },
        TakerPays: '20000000000',
        index: '606B3FC9199D5122F1DCC73EC1629E40C8A838D58AC67315A78D76699D960705',
        owner_funds: options.fourth_account_funds,
        quality: '69425159.67786726'
      },
      {
        Account: addresses.ACCOUNT,
        BookDirectory: '4627DFFCFF8B5A265EDBD8AE8C14A52325DBFEDAF4F5C32E5C18C3D9EF58005A',
        BookNode: '0000000000000000',
        Flags: 0,
        LedgerEntryType: 'Offer',
        OwnerNode: '0000000000006762',
        PreviousTxnID: 'E3A0240001B03E4F16C4BA6C2B0CB00C01413BE331ABE9E782B6A975DC936618',
        PreviousTxnLgrSeq: 11558318,
        Sequence: 1689755,
        TakerGets: {
          currency: 'USD',
          issuer: addresses.ISSUER,
          value: '196.460002'
        },
        TakerPays: '13694716399',
        index: '9A5D0AA37EA0889B876E9A3E552CACDB28BA5A3CD482A528992CD0CCFC09F6E8',
        quality: '69707402.31897178'
      }
    ]
  };
};

module.exports.MODIFIED_NODES = [
  {
    ModifiedNode: {
      FinalFields: {
        Account: addresses.ACCOUNT,
        BookDirectory: 'E2B91A0A170BCC2BEC5C44B492D9B672888D9267A900330F5C08953CAA35D973',
        BookNode: '0000000000000000',
        Flags: 131072,
        OwnerNode: '0000000000000001',
        Sequence: 538,
        TakerGets: {
          currency: 'USD',
          issuer: addresses.ISSUER,
          value: module.exports.TAKER_GETS_FINAL
        },
        TakerPays: module.exports.TAKER_PAYS_FINAL
      },
      LedgerEntryType: 'Offer',
      LedgerIndex: module.exports.LEDGER_INDEX,
      PreviousFields: {
        TakerGets: {
          currency: 'USD',
          issuer: addresses.ISSUER,
          value: module.exports.TAKER_GETS
        },
        TakerPays: module.exports.TAKER_PAYS
      },
      PreviousTxnID: '5135DF8678727A70491DE512E5F0FE586E7C1E866492293B8898BF8191CFCAEB',
      PreviousTxnLgrSeq: 11676651
    }
  },
  {
    ModifiedNode: {
      FinalFields: {
        Account: addresses.OTHER_ACCOUNT,
        BookDirectory: 'E2B91A0A170BCC2BEC5C44B492D9B672888D9267A900330F5C08953CAA35D973',
        BookNode: '0000000000000000',
        Flags: 131072,
        OwnerNode: '0000000000000001',
        Sequence: 538,
        TakerGets: {
          currency: 'USD',
          issuer: addresses.ISSUER,
          value: module.exports.OTHER_TAKER_GETS_FINAL
        },
        TakerPays: module.exports.OTHER_TAKER_PAYS_FINAL
      },
      LedgerEntryType: 'Offer',
      LedgerIndex: module.exports.OTHER_LEDGER_INDEX,
      PreviousFields: {
        TakerGets: {
          currency: 'USD',
          issuer: addresses.ISSUER,
          value: module.exports.OTHER_TAKER_GETS
        },
        TakerPays: module.exports.OTHER_TAKER_PAYS
      },
      PreviousTxnID: '5135DF8678727A70491DE512E5F0FE586E7C1E866492293B8898BF8191CFCAEB',
      PreviousTxnLgrSeq: 11676651
    }
  }
];

module.exports.transactionWithRippleState = function(options) {
  options = options || {};
  _.defaults(options, {
    issuer: addresses.ISSUER,
    account: addresses.ACCOUNT,
    balance: module.exports.FIAT_BALANCE
  });

  return {
    mmeta: new Meta({
      AffectedNodes: [{
        ModifiedNode: {
          FinalFields: {
            Balance: {
              currency: 'USD',
              issuer: 'rrrrrrrrrrrrrrrrrrrrBZbvji',
              value: options.balance
            },
            Flags: 131072,
            HighLimit: {
              currency: 'USD',
              issuer: options.issuer,
              value: '100'
            },
            HighNode: '0000000000000000',
            LowLimit: {
              currency: 'USD',
              issuer: options.account,
              value: '0'
            },
            LowNode: '0000000000000000'
          },
          PreviousFields: {
            Balance: {
              currency: 'USD',
              issuer: 'rrrrrrrrrrrrrrrrrrrrBZbvji',
              value: '0'
            }
          },
          LedgerEntryType: 'RippleState',
          LedgerIndex: 'EA4BF03B4700123CDFFB6EB09DC1D6E28D5CEB7F680FB00FC24BC1C3BB2DB959',
          PreviousTxnID: '53354D84BAE8FDFC3F4DA879D984D24B929E7FEB9100D2AD9EFCD2E126BCCDC8',
          PreviousTxnLgrSeq: 343570
        }
      }]
    })
  };
};

module.exports.transactionWithAccountRoot = function(options) {
  options = options || {};
  _.defaults(options, {
    account: addresses.ACCOUNT,
    balance: module.exports.NATIVE_BALANCE,
    previous_balance: module.exports.NATIVE_BALANCE_PREVIOUS
  });

  return {
    mmeta: new Meta({
      AffectedNodes: [{
        ModifiedNode: {
          FinalFields: {
            Account: options.account,
            Balance: options.balance,
            Flags: 0,
            OwnerCount: 1,
            Sequence: 2
          },
          LedgerEntryType: 'AccountRoot',
          LedgerIndex: '4F83A2CF7E70F77F79A307E6A472BFC2585B806A70833CCD1C26105BAE0D6E05',
          PreviousFields: {
            Balance: options.previous_balance,
            OwnerCount: 0,
            Sequence: 1
          },
          PreviousTxnID: 'B24159F8552C355D35E43623F0E5AD965ADBF034D482421529E2703904E1EC09',
          PreviousTxnLgrSeq: 16154
        }
      }]
    })
  };
};

module.exports.transactionWithInvalidAccountRoot = function(options) {
  options = options || {};
  _.defaults(options, {
    account: addresses.ACCOUNT,
    balance: module.exports.NATIVE_BALANCE
  });

  return {
    mmeta: new Meta({
      AffectedNodes: [{
        ModifiedNode: {
          FinalFields: {
            Account: options.account,
            Balance: options.balance,
            Flags: 0,
            OwnerCount: 3,
            Sequence: 188
          },
          LedgerEntryType: 'AccountRoot',
          LedgerIndex: 'B33FDD5CF3445E1A7F2BE9B06336BEBD73A5E3EE885D3EF93F7E3E2992E46F1A',
          PreviousTxnID: 'E9E1988A0F061679E5D14DE77DB0163CE0BBDC00F29E396FFD1DA0366E7D8904',
          PreviousTxnLgrSeq: 195455
        }
      }]
    })
  };
};

module.exports.transactionWithCreatedOffer = function(options) {
  options = options || {};
  _.defaults(options, {
    account: addresses.ACCOUNT,
    amount: '1.9951'
  });

  const meta = new Meta({
    AffectedNodes: [
      {
        CreatedNode: {
          LedgerEntryType: 'Offer',
          LedgerIndex: 'AF3C702057C9C47DB9E809FD8C76CD22521012C5CC7AE95D914EC9E226F1D7E5',
          NewFields: {
            Account: options.account,
            BookDirectory: '7B73A610A009249B0CC0D4311E8BA7927B5A34D86634581C5F211CEE1E0697A0',
            Flags: 131072,
            Sequence: 1404,
            TakerGets: {
              currency: 'USD',
              issuer: addresses.ISSUER,
              value: options.amount
            },
            TakerPays: module.exports.TAKER_PAYS
          }
        }
      }
    ]
  });

  meta.getAffectedBooks();

  return {
    mmeta: meta,
    transaction: {
      TransactionType: 'OfferCreate',
      owner_funds: '2010.027702881682'
    }
  };
};

module.exports.transactionWithDeletedOffer = function(options) {
  options = options || {};
  _.defaults(options, {
    transaction_type: 'OfferCreate'
  });

  const meta = new Meta({
    AffectedNodes: [
      {
        DeletedNode: {
          FinalFields: {
            Account: addresses.ACCOUNT,
            BookDirectory: '3B95C29205977C2136BBC70F21895F8C8F471C8522BF446E570463F9CDB31517',
            BookNode: '0000000000000000',
            Expiration: 477086871,
            Flags: 131072,
            OwnerNode: '0000000000000979',
            PreviousTxnID: 'DDD2AB60A2AA1690A6CB99B094BFD2E39A81AFF2AA91B5E4049D2C96A4BC8EBA',
            PreviousTxnLgrSeq: 11674760,
            Sequence: 85006,
            TakerGets: {
              currency: 'USD',
              issuer: addresses.ISSUER,
              value: module.exports.TAKER_GETS
            },
            TakerPays: module.exports.TAKER_PAYS
          },
          LedgerEntryType: 'Offer',
          LedgerIndex: module.exports.LEDGER_INDEX
        }
      }
    ]
  });

  meta.getAffectedBooks();

  return {
    mmeta: meta,
    transaction: {
      TransactionType: options.transaction_type,
      owner_funds: '2010.027702881682'
    }
  };
};

module.exports.transactionWithDeletedOfferR = function(options) {
  options = options || {};
  _.defaults(options, {
    transaction_type: 'OfferCreate'
  });

  const meta = new Meta({
    AffectedNodes: [
      {
        DeletedNode: {
          FinalFields: {
            Account: addresses.ACCOUNT,
            BookDirectory: '3B95C29205977C2136BBC70F21895F8C8F471C8522BF446E570463F9CDB31517',
            BookNode: '0000000000000000',
            Expiration: 477086871,
            Flags: 131072,
            OwnerNode: '0000000000000979',
            PreviousTxnID: 'DDD2AB60A2AA1690A6CB99B094BFD2E39A81AFF2AA91B5E4049D2C96A4BC8EBA',
            PreviousTxnLgrSeq: 11674760,
            Sequence: 85006,
            TakerPays: {
              currency: 'USD',
              issuer: addresses.ISSUER,
              value: module.exports.TAKER_GETS
            },
            TakerGets: module.exports.TAKER_PAYS
          },
          LedgerEntryType: 'Offer',
          LedgerIndex: 'B6BC3B0F87976370EE11F5575593FE63AA5DC1D602830DC96F04B2D597F044BF'
        }
      }
    ]
  });

  meta.getAffectedBooks();

  return {
    mmeta: meta,
    transaction: {
      TransactionType: options.transaction_type,
      owner_funds: '2010.027702881682'
    }
  };
};

module.exports.transactionWithModifiedOffer = function() {
  const meta = new Meta({
    AffectedNodes: module.exports.MODIFIED_NODES.slice(0, 1)
  });

  meta.getAffectedBooks();

  return {
    mmeta: meta,
    transaction: {
      TransactionType: 'OfferCreate',
      owner_funds: '2010.027702881682'
    }
  };
};

module.exports.transactionWithModifiedOffers = function() {
  const meta = new Meta({
    AffectedNodes: module.exports.MODIFIED_NODES
  });

  meta.getAffectedBooks();

  return {
    mmeta: meta,
    transaction: {
      TransactionType: 'OfferCreate',
      owner_funds: '2010.027702881682'
    }
  };
};

module.exports.transactionWithNoNodes = function() {
  const meta = new Meta({
    AffectedNodes: []
  });

  meta.getAffectedBooks();

  return {
    mmeta: meta,
    transaction: {
      TransactionType: 'OfferCreate',
      owner_funds: '2010.027702881682'
    }
  };
};

module.exports.accountInfoResponse = function(options) {
  options = options || {};
  _.defaults(options, {
    account: addresses.ISSUER
  });

  return {
    account_data: {
      Account: options.account,
      Balance: '6156166959471',
      Domain: '6269747374616D702E6E6574',
      EmailHash: '5B33B93C7FFE384D53450FC666BB11FB',
      Flags: 131072,
      LedgerEntryType: 'AccountRoot',
      OwnerCount: 0,
      PreviousTxnID: '6A7D0AB36CBA6884FDC398254BC67DE7E0B4887E9B0252568391102FBB854C09',
      PreviousTxnLgrSeq: 8344426,
      Sequence: 561,
      TransferRate: module.exports.TRANSFER_RATE,
      index: 'B7D526FDDF9E3B3F95C3DC97C353065B0482302500BBB8051A5C090B596C6133',
      urlgravatar: 'http:www.gravatar.com/avatar/5b33b93c7ffe384d53450fc666bb11fb'
    }
  };
};
