/** @file
 *  Aggregator entry point for injecting synthetic NFT fields into RPC
 *  transaction responses.
 *
 *  "Synthetic" fields (`nftoken_ids`, `nftoken_id`, `offer_id`) are derived
 *  at query time from the ledger state changes recorded in `TxMeta`; they are
 *  not stored on-chain.  Callers invoke a single function here rather than
 *  calling the individual NFT inserters directly, keeping call sites from
 *  accumulating an ever-growing list of per-type injector calls as new NFT
 *  transaction types are added.
 *
 *  The underlying extraction helpers (`insertNFTokenID`, `insertNFTokenOfferID`)
 *  live in `NFTokenID.h` and `NFTokenOfferID.h` under the broader `xrpl::`
 *  namespace so that Clio (the XRPL History API server) can call those helpers
 *  directly without the `xrpl::RPC` coupling imposed by this header.
 */

#pragma once

#include <xrpl/json/json_forwards.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxMeta.h>

#include <memory>

namespace xrpl::RPC {

/** Enrich a transaction JSON response with NFT-derived synthetic fields.
 *
 *  Delegates to two independent inserters, in order:
 *
 *  - `insertNFTokenID` — adds `nftoken_id` (for `NFTokenMint` and
 *    `NFTokenAcceptOffer`) or `nftoken_ids` (for `NFTokenCancelOffer`) by
 *    diffing the NFToken arrays across all affected ledger nodes recorded in
 *    the transaction metadata.
 *  - `insertNFTokenOfferID` — adds `offer_id` for `NFTokenCreateOffer` (and
 *    mints that include an immediate sell offer) by locating the newly created
 *    `NFTokenOffer` node and extracting its `sfLedgerIndex`.
 *
 *  Both delegates gate themselves on transaction type and `tesSUCCESS`, so
 *  this function is safe to call for any transaction type: non-NFT
 *  transactions produce no output.
 *
 *  Synthetic fields are written into `response[jss::meta]`.  The `meta`
 *  sub-object should already be populated by the caller (e.g., via
 *  `TxMeta::getJson`) before this function is invoked — consistent with the
 *  call-site pattern in `Tx.cpp`, `Simulate.cpp`, `AccountTx.cpp`, and
 *  `NetworkOPs.cpp`, where this call appears alongside `insertDeliveredAmount`
 *  and `insertMPTokenIssuanceID` as part of a fixed metadata-enrichment
 *  sequence.
 *
 *  @param response        Top-level RPC response object.  Synthetic fields are
 *      written into its `meta` sub-object, which is created on demand if
 *      absent.
 *  @param transaction     The executed transaction.  A null pointer is handled
 *      gracefully by the delegates (no-op).
 *  @param transactionMeta Read-only view of the transaction's metadata used to
 *      diff ledger node states and locate newly created objects.
 *
 *  @see xrpl::insertNFTokenID, xrpl::insertNFTokenOfferID
 */
void
insertNFTSyntheticInJson(
    json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta);

}  // namespace xrpl::RPC
