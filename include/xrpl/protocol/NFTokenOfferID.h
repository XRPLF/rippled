/** @file
 *  Helpers that recover the ledger index of a newly created `NFTokenOffer`
 *  from transaction metadata and inject it into RPC JSON responses as a
 *  synthetic `offer_id` field.
 *
 *  The XRPL transaction format records only inputs; the ledger index of a
 *  newly created offer object appears solely in the `CreatedNode` entries of
 *  the transaction metadata. The three functions below encapsulate the scan
 *  once so that every API consumer — rippled RPC handlers and Clio alike —
 *  can obtain the offer ID without walking `AffectedNodes` manually. All
 *  three functions are free (non-static) so that Clio can call them directly
 *  without duplicating the logic.
 *
 *  @see NFTokenID.h for the analogous helpers that inject `nftoken_id` /
 *      `nftoken_ids` for mint, accept-offer, and cancel-offer operations.
 */

#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxMeta.h>

#include <memory>
#include <optional>

namespace xrpl {

/** Determine whether a transaction can have an NFToken offer ID.
 *
 *  Acts as a cheap pre-filter before the metadata scan in
 *  `getOfferIDFromCreatedOffer`. Three conditions must all hold:
 *
 *  1. `serializedTx` is non-null.
 *  2. The transaction type is `ttNFTOKEN_CREATE_OFFER`, **or** it is
 *     `ttNFTOKEN_MINT` with `sfAmount` present (a mint that simultaneously
 *     creates an immediate-sale offer).
 *  3. The transaction succeeded (`tesSUCCESS`). A failed transaction never
 *     modifies the ledger, so no offer object can exist in the metadata.
 *
 *  @param serializedTx     The transaction to inspect. A null `shared_ptr`
 *      is handled safely and causes the function to return `false`.
 *  @param transactionMeta  Metadata whose result code is checked for success.
 *  @return `true` only when all three conditions are satisfied, indicating
 *      that a subsequent call to `getOfferIDFromCreatedOffer` may yield a
 *      value.
 */
bool
canHaveNFTokenOfferID(
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta);

/** Extract the ledger index of the NFToken offer created by a transaction.
 *
 *  Scans the `AffectedNodes` array in `transactionMeta` for a `CreatedNode`
 *  whose `sfLedgerEntryType` is `ltNFTOKEN_OFFER`. Modified and deleted
 *  nodes are skipped. The first qualifying node's `sfLedgerIndex` is
 *  returned; because at most one `NFTokenOffer` can be created per
 *  transaction, the loop exits immediately on the first match.
 *
 *  @param transactionMeta  Read-only transaction metadata to scan.
 *  @return The `uint256` ledger index of the newly created offer, or
 *      `std::nullopt` if no `CreatedNode` of type `ltNFTOKEN_OFFER` is
 *      found. Absence is a plausible non-exceptional condition (e.g., when
 *      processing historical or externally sourced transactions with
 *      incomplete metadata), not an error.
 *
 *  @note Callers that have already performed their own eligibility checks
 *      (e.g., Clio) may call this function directly without first calling
 *      `canHaveNFTokenOfferID`.
 */
std::optional<uint256>
getOfferIDFromCreatedOffer(TxMeta const& transactionMeta);

/** Inject the NFToken offer ID into a JSON response as `jss::offer_id`.
 *
 *  Composes `canHaveNFTokenOfferID` and `getOfferIDFromCreatedOffer`:
 *  returns immediately without touching `response` if the transaction is
 *  ineligible or the metadata contains no created offer node. When an offer
 *  ID is successfully extracted, it is written into `response[jss::offer_id]`
 *  as a hex string.
 *
 *  The primary call site is `xrpl::RPC::insertNFTSyntheticInJson`, which
 *  passes `response[jss::meta]` as the target so that `offer_id` appears
 *  inside the `meta` sub-object alongside the raw node data.
 *
 *  @param response         The JSON object to enrich; `jss::offer_id` is
 *      written directly into it on success. The caller is responsible for
 *      scoping this to `jss::meta` of the full response.
 *  @param transaction      The executed transaction. A null pointer is
 *      handled gracefully (no-op) via `canHaveNFTokenOfferID`.
 *  @param transactionMeta  Read-only transaction metadata used to locate the
 *      created `NFTokenOffer` node.
 */
void
insertNFTokenOfferID(
    json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta);

}  // namespace xrpl
