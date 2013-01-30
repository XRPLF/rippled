var ST = require("./serializedtypes");

var REQUIRED = exports.REQUIRED = 0,
    OPTIONAL = exports.OPTIONAL = 1,
    DEFAULT  = exports.DEFAULT  = 2;

ST.Int16.id               = 1;
ST.Int32.id               = 2;
ST.Int64.id               = 3;
ST.Hash128.id             = 4;
ST.Hash256.id             = 5;
ST.Amount.id              = 6;
ST.VariableLength.id      = 7;
ST.Account.id             = 8;
ST.Object.id              = 14;
ST.Array.id               = 15;
ST.Int8.id                = 16;
ST.Hash160.id             = 17;
ST.PathSet.id             = 18;
ST.Vector256.id           = 19;

var base = [
  [ 'TransactionType'    , REQUIRED,  2, ST.Int16 ],
  [ 'Flags'              , OPTIONAL,  2, ST.Int32 ],
  [ 'SourceTag'          , OPTIONAL,  3, ST.Int32 ],
  [ 'Account'            , REQUIRED,  1, ST.Account ],
  [ 'Sequence'           , REQUIRED,  4, ST.Int32 ],
  [ 'Fee'                , REQUIRED,  8, ST.Amount ],
  [ 'OperationLimit'     , OPTIONAL, 29, ST.Int32 ],
  [ 'SigningPubKey'      , REQUIRED,  3, ST.VariableLength ],
  [ 'TxnSignature'       , OPTIONAL,  4, ST.VariableLength ]
];

exports.tx = {
  AccountSet: [3].concat(base, [
    [ 'EmailHash'          , OPTIONAL,  1, ST.Hash128 ],
    [ 'WalletLocator'      , OPTIONAL,  7, ST.Hash256 ],
    [ 'WalletSize'         , OPTIONAL, 12, ST.Int32 ],
    [ 'MessageKey'         , OPTIONAL,  2, ST.VariableLength ],
    [ 'Domain'             , OPTIONAL,  7, ST.VariableLength ],
    [ 'TransferRate'       , OPTIONAL, 11, ST.Int32 ]
  ]),
  TrustSet: [20].concat(base, [
    [ 'LimitAmount'        , OPTIONAL,  3, ST.Amount ],
    [ 'QualityIn'          , OPTIONAL, 20, ST.Int32 ],
    [ 'QualityOut'         , OPTIONAL, 21, ST.Int32 ]
  ]),
  OfferCreate: [7].concat(base, [
    [ 'TakerPays'          , REQUIRED,  4, ST.Amount ],
    [ 'TakerGets'          , REQUIRED,  5, ST.Amount ],
    [ 'Expiration'         , OPTIONAL, 10, ST.Int32 ]
  ]),
  OfferCancel: [8].concat(base, [
    [ 'OfferSequence'      , REQUIRED, 25, ST.Int32 ]
  ]),
  SetRegularKey: [5].concat(base, [
    [ 'RegularKey'         , REQUIRED,  8, ST.Account ]
  ]),
  Payment: [0].concat(base, [
    [ 'Destination'        , REQUIRED,  3, ST.Account ],
    [ 'Amount'             , REQUIRED,  1, ST.Amount ],
    [ 'SendMax'            , OPTIONAL,  9, ST.Amount ],
    [ 'Paths'              , DEFAULT ,  1, ST.PathSet ],
    [ 'InvoiceID'          , OPTIONAL, 17, ST.Hash256 ],
    [ 'DestinationTag'     , OPTIONAL, 14, ST.Int32 ]
  ]),
  Contract: [9].concat(base, [
    [ 'Expiration'         , REQUIRED, 10, ST.Int32 ],
    [ 'BondAmount'         , REQUIRED, 23, ST.Int32 ],
    [ 'StampEscrow'        , REQUIRED, 22, ST.Int32 ],
    [ 'RippleEscrow'       , REQUIRED, 17, ST.Amount ],
    [ 'CreateCode'         , OPTIONAL, 11, ST.VariableLength ],
    [ 'FundCode'           , OPTIONAL,  8, ST.VariableLength ],
    [ 'RemoveCode'         , OPTIONAL,  9, ST.VariableLength ],
    [ 'ExpireCode'         , OPTIONAL, 10, ST.VariableLength ]
  ]),
  RemoveContract: [10].concat(base, [
    [ 'Target'             , REQUIRED,  7, ST.Account ]
  ]),
  EnableFeature: [100].concat(base, [
    [ 'Feature'            , REQUIRED, 19, ST.Hash256 ]
  ]),
  SetFee: [101].concat(base, [
    [ 'Features'           , REQUIRED,  9, ST.Array ],
    [ 'BaseFee'            , REQUIRED,  5, ST.Int64 ],
    [ 'ReferenceFeeUnits'  , REQUIRED, 30, ST.Int32 ],
    [ 'ReserveBase'        , REQUIRED, 31, ST.Int32 ],
    [ 'ReserveIncrement'   , REQUIRED, 32, ST.Int32 ]
  ])
};
