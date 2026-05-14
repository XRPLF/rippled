#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxMeta.h>

#include <memory>
#include <optional>

namespace xrpl::RPC {

/** @file
 *  Declares the three-function enrichment pipeline that injects
 *  `mpt_issuance_id` into RPC JSON responses for successful
 *  `MPTokenIssuanceCreate` transactions.
 *
 *  `MPTID` is a 192-bit identifier derived at runtime from the `Sequence`
 *  and `Issuer` fields inside the `MPTokenIssuance` ledger entry created by
 *  the transaction; it is not stored redundantly in the transaction itself.
 *  These functions bridge that gap by extracting the ID from the transaction's
 *  `CreatedNode` metadata and injecting it into the JSON response.
 *
 *  @see insertDeliveredAmount(), RPC::insertNFTSyntheticInJson() — sibling
 *      enrichment functions that follow the same pattern and are always
 *      applied at the same call sites.
 */

/** Determine whether a transaction can carry an `mpt_issuance_id` field.
 *
 *  Returns `true` only when both of the following hold:
 *  - `serializedTx` is non-null and its type is `ttMPTOKEN_ISSUANCE_CREATE`.
 *  - `transactionMeta` reports `tesSUCCESS`.
 *
 *  A failed `MPTokenIssuanceCreate` does not create a ledger entry, so there
 *  is no `CreatedNode` to scan and no identifier to inject. This predicate
 *  is exposed separately from `insertMPTokenIssuanceID` to allow unit tests
 *  to verify eligibility without constructing a full JSON response object.
 *
 *  @param serializedTx The signed transaction to inspect; may be null.
 *  @param transactionMeta The execution metadata for the transaction.
 *  @return `true` if the transaction is a successful `MPTokenIssuanceCreate`,
 *      `false` otherwise.
 */
bool
canHaveMPTokenIssuanceID(
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta);

/** Extract the `MPTID` of the issuance created by a transaction.
 *
 *  Walks the affected-nodes list in `transactionMeta` looking for a
 *  `sfCreatedNode` whose `LedgerEntryType` is `ltMPTOKEN_ISSUANCE`. When
 *  found, reads `sfSequence` and `sfIssuer` from `sfNewFields` and returns
 *  `makeMptID(sequence, issuer)`.
 *
 *  Returns `std::nullopt` when no matching `CreatedNode` is present. Callers
 *  should guard with `canHaveMPTokenIssuanceID()` before invoking this
 *  function if they want to avoid scanning metadata for ineligible
 *  transactions.
 *
 *  @param transactionMeta The execution metadata to scan.
 *  @return The 192-bit `MPTID` of the newly created issuance, or
 *      `std::nullopt` if no `ltMPTOKEN_ISSUANCE` `CreatedNode` is found.
 */
std::optional<MPTID>
getIDFromCreatedIssuance(TxMeta const& transactionMeta);

/** Inject `mpt_issuance_id` into a JSON response for eligible transactions.
 *
 *  Calls `canHaveMPTokenIssuanceID()` and returns early if the transaction
 *  is not a successful `MPTokenIssuanceCreate`. Otherwise, delegates to
 *  `getIDFromCreatedIssuance()` and, if an ID is found, writes
 *  `response[jss::mpt_issuance_id]` as a stringified `MPTID`.
 *
 *  Must be called at every site where transaction metadata is serialized to
 *  JSON — alongside `insertDeliveredAmount()` and `insertNFTSyntheticInJson()`
 *  — so that API consumers always receive the derived identifier without
 *  having to recompute it.
 *
 *  @param response The JSON object to enrich in place; unchanged if the
 *      transaction is ineligible or no `CreatedNode` is found.
 *  @param transaction The signed transaction; may be null (treated as
 *      ineligible).
 *  @param transactionMeta The execution metadata for the transaction.
 */
void
insertMPTokenIssuanceID(
    json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta);

}  // namespace xrpl::RPC
