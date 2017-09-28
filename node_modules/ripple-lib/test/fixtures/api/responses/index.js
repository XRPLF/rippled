'use strict';

module.exports = {
  generateAddress: require('./generate-address.json'),
  getAccountInfo: require('./get-account-info.json'),
  getBalances: require('./get-balances.json'),
  getOrderbook: require('./get-orderbook.json'),
  getOrders: require('./get-orders.json'),
  getPaths: {
    XrpToUsd: require('./get-paths.json'),
    UsdToUsd: require('./get-paths-send-usd.json'),
    XrpToXrp: require('./get-paths-xrp-to-xrp.json')
  },
  getServerInfo: require('./get-server-info.json'),
  getSettings: require('./get-settings.json'),
  getTransaction: {
    orderCancellation: require('./get-transaction-order-cancellation.json'),
    order: require('./get-transaction-order.json'),
    payment: require('./get-transaction-payment.json'),
    settings: require('./get-transaction-settings.json'),
    trustline: require('./get-transaction-trustline-set.json'),
    trackingOn: require('./get-transaction-settings-tracking-on.json'),
    trackingOff: require('./get-transaction-settings-tracking-off.json'),
    setRegularKey: require('./get-transaction-settings-set-regular-key.json'),
    trustlineFrozenOff: require('./get-transaction-trust-set-frozen-off.json'),
    trustlineNoQuality: require('./get-transaction-trust-no-quality.json'),
    notValidated: require('./get-transaction-not-validated.json')
  },
  getTransactions: require('./get-transactions.json'),
  getTrustlines: require('./get-trustlines.json'),
  getLedger: {
    header: require('./get-ledger'),
    full: require('./get-ledger-full')
  },
  prepareOrderCancellation: require('./prepare-order-cancellation.json'),
  prepareOrder: require('./prepare-order.json'),
  prepareOrderSell: require('./prepare-order-sell.json'),
  preparePayment: require('./prepare-payment.json'),
  preparePaymentAllOptions: require('./prepare-payment-all-options.json'),
  preparePaymentNoCounterparty:
    require('./prepare-payment-no-counterparty.json'),
  prepareSettings: {
    regularKey: require('./prepare-settings-regular-key.json'),
    flags: require('./prepare-settings.json'),
    flagSet: require('./prepare-settings-flag-set.json'),
    flagClear: require('./prepare-settings-flag-clear.json'),
    setTransferRate: require('./prepare-settings-set-transfer-rate.json'),
    fieldClear: require('./prepare-settings-field-clear.json')
  },
  prepareTrustline: {
    simple: require('./prepare-trustline-simple.json'),
    complex: require('./prepare-trustline.json')
  },
  sign: require('./sign.json'),
  submit: require('./submit.json')
};
