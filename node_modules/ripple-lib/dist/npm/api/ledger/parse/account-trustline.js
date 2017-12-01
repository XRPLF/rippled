
'use strict';
var utils = require('./utils');

// rippled 'account_lines' returns a different format for
// trustlines than 'tx'
function parseAccountTrustline(trustline) {
  var specification = utils.removeUndefined({
    limit: trustline.limit,
    currency: trustline.currency,
    counterparty: trustline.account,
    qualityIn: trustline.quality_in || undefined,
    qualityOut: trustline.quality_out || undefined,
    ripplingDisabled: trustline.no_ripple || undefined,
    frozen: trustline.freeze || undefined,
    authorized: trustline.authorized || undefined
  });
  // rippled doesn't provide the counterparty's qualities
  var counterparty = utils.removeUndefined({
    limit: trustline.limit_peer,
    ripplingDisabled: trustline.no_ripple_peer || undefined,
    frozen: trustline.freeze_peer || undefined,
    authorized: trustline.peer_authorized || undefined
  });
  var state = {
    balance: trustline.balance
  };
  return { specification: specification, counterparty: counterparty, state: state };
}

module.exports = parseAccountTrustline;