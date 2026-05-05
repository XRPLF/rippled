#pragma once

#include <xrpl/protocol/TxFormats.h>

namespace json {
class Value;
}  // namespace json

namespace xrpl::RPC {

/**
   Copy `Amount` field to `DeliverMax` field in transaction output JSON.
   This only applies to Payment transaction type, all others are ignored.

   When apiVersion > 1 will also remove `Amount` field, forcing users
   to access this value using new `DeliverMax` field only.
   @{
 */

void
insertDeliverMax(json::Value& txJson, TxType txnType, unsigned int apiVersion);

/** @} */

}  // namespace xrpl::RPC
