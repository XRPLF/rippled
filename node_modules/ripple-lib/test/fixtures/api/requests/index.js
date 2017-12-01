'use strict';

module.exports = {
  prepareOrder: require('./prepare-order'),
  prepareOrderSell: require('./prepare-order-sell'),
  preparePayment: require('./prepare-payment'),
  preparePaymentAllOptions: require('./prepare-payment-all-options'),
  preparePaymentNoCounterparty: require('./prepare-payment-no-counterparty'),
  prepareSettings: require('./prepare-settings'),
  prepareTrustline: {
    simple: require('./prepare-trustline-simple'),
    complex: require('./prepare-trustline')
  },
  sign: require('./sign'),
  getPaths: {
    normal: require('./getpaths/normal'),
    UsdToUsd: require('./getpaths/usd2usd'),
    XrpToXrp: require('./getpaths/xrp2xrp'),
    XrpToXrpNotEnough: require('./getpaths/xrp2xrp-not-enough'),
    NotAcceptCurrency: require('./getpaths/not-accept-currency'),
    NoPaths: require('./getpaths/no-paths'),
    NoPathsWithCurrencies: require('./getpaths/no-paths-with-currencies')
  },
  computeLedgerHash: {
    header: require('./compute-ledger-hash'),
    transactions: require('./compute-ledger-hash-transactions')
  }
};
