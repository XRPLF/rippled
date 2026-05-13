/** @file
 *  Utilities for computing and injecting the `delivered_amount` field into
 *  transaction metadata JSON responses.
 *
 *  The `Amount` field on a Payment (or CheckCash) transaction records the
 *  *intended* delivery, not the actual delivery. When `tfPartialPayment` is
 *  set, the ledger may settle for less. The `DeliveredAmount` field in
 *  `TxMeta` was introduced at ledger sequence 4594095 (January 24, 2014) to
 *  record the actual delivery. This module bridges the historical gap between
 *  old and new ledgers so RPC consumers always receive a reliable
 *  `delivered_amount` value.
 */

#pragma once

#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>

#include <functional>
#include <memory>

namespace json {
class Value;
}  // namespace json

namespace xrpl {

class ReadView;
class Transaction;
class TxMeta;
class STTx;

namespace RPC {

struct JsonContext;

struct Context;

/** Inject a `delivered_amount` field into a transaction metadata JSON object.
 *
 *  The field is added only for `ttPAYMENT`, `ttCHECK_CASH`, and
 *  `ttACCOUNT_DELETE` transactions that completed with `tesSUCCESS`. The
 *  resolution follows a three-tier fallback:
 *  1. Use `TxMeta::getDeliveredAmount()` if present — authoritative for all
 *     ledgers after sequence 4594095.
 *  2. If the field is absent but the ledger index is ≥ 4594095, or the ledger
 *     closed after NetClock timestamp 446000000s (February 2014), return the
 *     transaction's `Amount` field — absence of `DeliveredAmount` after
 *     deployment means full delivery.
 *  3. Otherwise write the string `"unavailable"` — a sentinel that cannot be
 *     parsed as a valid `STAmount`, preventing misinterpretation by consumers.
 *
 *  The ledger index and close time are read directly from `ledger.header()`,
 *  making this overload appropriate for full-ledger serialization in
 *  `LedgerToJson`.
 *
 *  @param meta The `meta` JSON object to mutate; `delivered_amount` is
 *      written into this object.
 *  @param ledger The closed ledger whose header supplies the sequence number
 *      and close time used for the historical threshold check.
 *  @param serializedTx The serialized transaction being described.
 *  @param transactionMeta The transaction metadata associated with
 *      `serializedTx` in `ledger`.
 *
 *  @{
 */
void
insertDeliveredAmount(
    json::Value& meta,
    ReadView const& ledger,
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta);

/** Inject a `delivered_amount` field into a transaction metadata JSON object.
 *
 *  Overload for use in `tx` and `account_tx` RPC handlers where the ledger
 *  is not directly available. The ledger sequence is derived from
 *  `TxMeta::getLgrSeq()`; the close time is fetched lazily via
 *  `context.ledgerMaster.getCloseTimeBySeq()` — the lookup is skipped
 *  entirely when `TxMeta` already carries `DeliveredAmount`, which is the
 *  common case for modern ledgers.
 *
 *  This overload unwraps the application-level `Transaction` object and
 *  delegates to the `STTx const` overload below.
 *
 *  @param meta The `meta` JSON object to mutate.
 *  @param context The RPC dispatch context; `context.ledgerMaster` is used
 *      for the lazy close-time lookup.
 *  @param transaction The application-level transaction wrapper whose inner
 *      `STTx` is used for type and amount inspection.
 *  @param transactionMeta The transaction metadata for `transaction`.
 */
void
insertDeliveredAmount(
    json::Value& meta,
    RPC::JsonContext const& context,
    std::shared_ptr<Transaction> const& transaction,
    TxMeta const& transactionMeta);

/** Inject a `delivered_amount` field into a transaction metadata JSON object.
 *
 *  Overload for use in `tx` and `account_tx` RPC handlers operating directly
 *  on a serialized transaction. See the `Transaction` overload above for
 *  full behavioral description. Both overloads use `TxMeta::getLgrSeq()` as
 *  the ledger index source and lazily resolve the close time through
 *  `context.ledgerMaster`.
 *
 *  @param meta The `meta` JSON object to mutate.
 *  @param context The RPC dispatch context; `context.ledgerMaster` is used
 *      for the lazy close-time lookup.
 *  @param transaction The serialized transaction.
 *  @param transactionMeta The transaction metadata for `transaction`.
 */
void
insertDeliveredAmount(
    json::Value& meta,
    RPC::JsonContext const& context,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta);

/** @} */

/** Resolve the delivered amount for a transaction without mutating JSON.
 *
 *  Applies the same three-tier source resolution as `insertDeliveredAmount`
 *  but returns the value rather than writing it into a JSON object. Returns
 *  `std::nullopt` when the transaction type or result is not eligible, or
 *  when the ledger predates the `DeliveredAmount` deployment and no
 *  authoritative value can be determined (the `"unavailable"` sentinel is
 *  not expressible as an `STAmount`).
 *
 *  Uses the base `RPC::Context` rather than `RPC::JsonContext` because it
 *  does not need request parameters — only `context.ledgerMaster` for the
 *  lazy close-time lookup.
 *
 *  @param context The RPC context; `context.ledgerMaster` is queried for
 *      the close time of `ledgerIndex` when needed.
 *  @param serializedTx The serialized transaction to inspect.
 *  @param transactionMeta The transaction metadata; checked for eligibility
 *      (type must be `ttPAYMENT`, `ttCHECK_CASH`, or `ttACCOUNT_DELETE`;
 *      result must be `tesSUCCESS`) and for the `sfDeliveredAmount` field.
 *  @param ledgerIndex The sequence number of the ledger that applied
 *      `serializedTx`; used as the primary historical threshold check.
 *  @return The resolved delivered amount, or `std::nullopt` if the
 *      transaction is ineligible or the amount cannot be determined.
 *  @note This is intended for callers such as `Simulate` that need to
 *      reason about the delivered amount without constructing the full JSON
 *      response inline.
 */
std::optional<STAmount>
getDeliveredAmount(
    RPC::Context const& context,
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta,
    LedgerIndex const& ledgerIndex);

}  // namespace RPC
}  // namespace xrpl
