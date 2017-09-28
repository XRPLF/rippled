'use strict';

const _ = require('lodash');


module.exports.payment = function(_options = {}) {
  const options = _options;
  _.defaults(options, {
    value: '0.000001',
    currency: 'XRP',
    issuer: ''
  });

  const paymentObject = {
    source: {
      address: options.sourceAccount,
      amount: {
        value: options.value,
        currency: options.currency
      }
    },
    destination: {
      address: options.destinationAccount,
      amount: {
        value: options.value,
        currency: options.currency
      }
    }
  };

  if (options.issuer) {
    paymentObject.source.amount.counterparty = options.issuer;
    paymentObject.destination.amount.counterparty = options.issuer;
  }
  return paymentObject;
};
