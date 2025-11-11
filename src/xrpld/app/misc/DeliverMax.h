#ifndef XRPL_APP_MISC_DELIVERMAX_H_INCLUDED
#define XRPL_APP_MISC_DELIVERMAX_H_INCLUDED

#include <xrpl/protocol/TxFormats.h>

namespace Json {
class Value;
}

namespace ripple {

namespace RPC {

/**
   Copy `Amount` field to `DeliverMax` field in transaction output JSON.
   This only applies to Payment transaction type, all others are ignored.

   When apiVersion > 1 will also remove `Amount` field, forcing users
   to access this value using new `DeliverMax` field only.
   @{
 */

void
insertDeliverMax(Json::Value& tx_json, TxType txnType, unsigned int apiVersion);

/** @} */

}  // namespace RPC
}  // namespace ripple

#endif
