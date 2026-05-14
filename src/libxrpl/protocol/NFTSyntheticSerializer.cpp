/** @file
 *  Composition point for NFT synthetic field injection into RPC responses.
 *
 *  Provides `xrpl::RPC::insertNFTSyntheticInJson`, the single entry point
 *  that RPC handlers call after writing raw transaction metadata to enrich
 *  a response with NFT-derived fields. The underlying extraction logic lives
 *  in `NFTokenID.h` and `NFTokenOfferID.h` under the broader `xrpl::`
 *  namespace so that Clio (the XRPL History API server) can call those
 *  helpers directly without the RPC coupling.
 */

#include <xrpl/protocol/NFTSyntheticSerializer.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/NFTokenID.h>
#include <xrpl/protocol/NFTokenOfferID.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/jss.h>

#include <memory>

namespace xrpl::RPC {

/** Enrich a transaction JSON response with NFT-derived synthetic fields.
 *
 *  Writes into `response[jss::meta]`, calling two independent delegates:
 *
 *  - `insertNFTokenID` — adds `nftoken_id` (mint/accept-offer) or
 *    `nftoken_ids` (cancel-offer) by comparing pre- and post-transaction
 *    NFToken arrays across all affected ledger nodes in the metadata.
 *  - `insertNFTokenOfferID` — adds `offer_id` for `NFTokenCreateOffer`
 *    (and mints with an immediate sell offer) by locating the newly
 *    created `NFTokenOffer` node and extracting its `sfLedgerIndex`.
 *
 *  Both delegates perform their own eligibility checks gated on transaction
 *  type and `tesSUCCESS`, so this function is safe to call for any
 *  transaction type: non-NFT transactions produce no output.
 *
 *  @param response         The top-level RPC response object; synthetic
 *      fields are written into its `meta` sub-object, which is created on
 *      demand if absent.
 *  @param transaction      The executed transaction. A null pointer is
 *      handled gracefully by the delegates (no-op).
 *  @param transactionMeta  Read-only view of the transaction's metadata;
 *      used to diff ledger node states and locate created objects.
 *
 *  @note The `meta` sub-object of `response` must already be populated by
 *      the caller (e.g., via `TxMeta::getJson`) before this function is
 *      invoked, matching the call-site pattern in `Tx.cpp`, `Simulate.cpp`,
 *      `AccountTx.cpp`, and `NetworkOPs.cpp`.
 *
 *  @see insertNFTokenID, insertNFTokenOfferID
 */
void
insertNFTSyntheticInJson(
    json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    insertNFTokenID(response[jss::meta], transaction, transactionMeta);
    insertNFTokenOfferID(response[jss::meta], transaction, transactionMeta);
}

}  // namespace xrpl::RPC
