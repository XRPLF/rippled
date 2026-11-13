#pragma once

#include <xrpl/protocol/TxFormats.h>

#include <expected>

namespace json {
class Value;
}  // namespace json

namespace xrpl::rpc {

/**
 * Whether `TransactionType` names a Payment in a form `STParsedJSON` reads:
 * the name, an integer, or a string of digits.
 *
 * @param txJson The `tx_json` object of a request.
 * @return true when the transaction is a Payment.
 */
[[nodiscard]] bool
isPayment(json::Value const& txJson);

/**
 * Copy `Amount` field to `DeliverMax` field in transaction output JSON.
 * This applies to a Payment and to each inner Payment in the
 * `RawTransactions` of a Batch; all other transaction types are ignored.
 *
 * When apiVersion > 1 will also remove `Amount` field, forcing users
 * to access this value using new `DeliverMax` field only.
 */
/** @{ */

void
insertDeliverMax(json::Value& txJson, TxType txnType, unsigned int apiVersion);

/** @} */

/**
 * Replace the `DeliverMax` alias with `Amount` in transaction input JSON.
 * This applies to a Payment and to each inner Payment in the
 * `RawTransactions` of a Batch; all other transaction types are ignored.
 * Malformed `RawTransactions` entries are left for the JSON parser to report.
 *
 * @param txJson The `tx_json` object of a request.
 * @return The `invalidParams` error when a Payment specifies differing
 *         `Amount` and `DeliverMax`.
 */
[[nodiscard]] std::expected<void, json::Value>
replaceDeliverMaxWithAmount(json::Value& txJson);

}  // namespace xrpl::rpc
