'use strict';

module.exports = {
  submit: require('./submit'),
  ledger: require('./ledger'),
  ledgerNotFound: require('./ledger-not-found'),
  ledgerWithoutCloseTime: require('./ledger-without-close-time'),
  subscribe: require('./subscribe'),
  unsubscribe: require('./unsubscribe'),
  account_info: {
    normal: require('./account-info'),
    notfound: require('./account-info-not-found')
  },
  account_offers: require('./account-offers'),
  account_tx: require('./account-tx'),
  book_offers: require('./book-offers'),
  server_info: require('./server-info'),
  server_info_error: require('./server-info-error'),
  path_find: {
    generate: require('./path-find'),
    sendUSD: require('./path-find-send-usd'),
    XrpToXrp: require('./path-find-xrp-to-xrp'),
    srcActNotFound: require('./path-find-srcActNotFound')
  },
  tx: {
    Payment: require('./tx/payment.json'),
    AccountSet: require('./tx/account-set.json'),
    AccountSetTrackingOn: require('./tx/account-set-tracking-on.json'),
    AccountSetTrackingOff: require('./tx/account-set-tracking-off.json'),
    RegularKey: require('./tx/set-regular-key.json'),
    OfferCreate: require('./tx/offer-create.json'),
    OfferCancel: require('./tx/offer-cancel.json'),
    TrustSet: require('./tx/trust-set.json'),
    TrustSetFrozenOff: require('./tx/trust-set-frozen-off.json'),
    TrustSetNoQuality: require('./tx/trust-set-no-quality.json'),
    NotFound: require('./tx/not-found.json'),
    NoLedgerIndex: require('./tx/no-ledger-index.json'),
    NoLedgerFound: require('./tx/no-ledger-found.json'),
    LedgerWithoutTime: require('./tx/ledger-without-time.json'),
    NotValidated: require('./tx/not-validated.json')
  }
};
