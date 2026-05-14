/** @file
 *  Synthetic `offer_id` injection for NFToken offer transactions.
 *
 *  The XRPL transaction format records only inputs; the ledger index of a
 *  newly created offer object is determined during consensus and stored only
 *  in transaction metadata. The three functions in this file extract that
 *  index from `TxMeta` and expose it as `jss::offer_id` in the RPC JSON
 *  response, sparing every API consumer from walking `AffectedNodes` manually.
 *
 *  These functions are non-static so that Clio (the XRPL History API server)
 *  can call `canHaveNFTokenOfferID` and `getOfferIDFromCreatedOffer`
 *  independently. The primary call site inside rippled is
 *  `xrpl::RPC::insertNFTSyntheticInJson` in `NFTSyntheticSerializer.cpp`.
 */

#include <xrpl/protocol/NFTokenOfferID.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/jss.h>

#include <memory>
#include <optional>

namespace xrpl {

/** Determine whether a transaction can have an NFToken offer ID.
 *
 *  Acts as a fast pre-filter before the more expensive metadata scan in
 *  `getOfferIDFromCreatedOffer`. Three conditions must all hold:
 *
 *  1. `serializedTx` is non-null.
 *  2. The transaction type is `ttNFTOKEN_CREATE_OFFER`, **or** it is
 *     `ttNFTOKEN_MINT` with `sfAmount` present (a mint that simultaneously
 *     creates an immediate-sale buy offer).
 *  3. The transaction succeeded (`tesSUCCESS`). Failed transactions never
 *     modify the ledger, so no offer object can exist in the metadata.
 *
 *  @param serializedTx     The transaction to inspect. A null `shared_ptr`
 *      is handled safely and causes the function to return `false`.
 *  @param transactionMeta  Metadata whose result code is checked for success.
 *  @return `true` only when all three conditions are satisfied, meaning a
 *      subsequent call to `getOfferIDFromCreatedOffer` may yield a value.
 */
bool
canHaveNFTokenOfferID(
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta)
{
    if (!serializedTx)
        return false;

    TxType const tt = serializedTx->getTxnType();
    if ((tt != ttNFTOKEN_MINT || !serializedTx->isFieldPresent(sfAmount)) &&
        tt != ttNFTOKEN_CREATE_OFFER)
        return false;

    // if the transaction failed nothing could have been delivered.
    if (!isTesSuccess(transactionMeta.getResultTER()))
        return false;

    return true;
}

/** Extract the ledger index of the NFToken offer created by a transaction.
 *
 *  Scans the `AffectedNodes` array in `transactionMeta` for the unique
 *  `CreatedNode` whose `sfLedgerEntryType` is `ltNFTOKEN_OFFER`. When found,
 *  returns `sfLedgerIndex` of that node — the globally unique offer ID.
 *
 *  Because at most one `NFTokenOffer` object can be created per transaction,
 *  the loop exits on the first match. Returns `std::nullopt` if no qualifying
 *  node is found, which can occur on edge cases where metadata is absent or
 *  the offer creation path was not actually taken despite an eligible type.
 *
 *  @param transactionMeta  Read-only transaction metadata to scan.
 *  @return The `uint256` ledger index of the newly created offer, or
 *      `std::nullopt` if no `CreatedNode` of type `ltNFTOKEN_OFFER` is
 *      present in the metadata.
 *
 *  @note Callers that have already performed their own eligibility checks
 *      (e.g., Clio) may call this function directly without first calling
 *      `canHaveNFTokenOfferID`.
 */
std::optional<uint256>
getOfferIDFromCreatedOffer(TxMeta const& transactionMeta)
{
    for (STObject const& node : transactionMeta.getNodes())
    {
        if (node.getFieldU16(sfLedgerEntryType) != ltNFTOKEN_OFFER ||
            node.getFName() != sfCreatedNode)
            continue;

        return node.getFieldH256(sfLedgerIndex);
    }
    return std::nullopt;
}

/** Inject the NFToken offer ID into a JSON response as `jss::offer_id`.
 *
 *  Composes `canHaveNFTokenOfferID` and `getOfferIDFromCreatedOffer`:
 *  returns immediately (no-op) if the transaction is ineligible or the
 *  metadata contains no created offer node; otherwise writes the offer's
 *  ledger index as a hex string into `response[jss::offer_id]`.
 *
 *  Never throws and never modifies `response` when the offer ID cannot be
 *  determined. The primary call site is
 *  `xrpl::RPC::insertNFTSyntheticInJson`, which passes `response[jss::meta]`
 *  as the target so that `offer_id` appears inside the `meta` sub-object.
 *
 *  @param response         The JSON object to enrich; `jss::offer_id` is
 *      written here when a valid offer ID is found.
 *  @param transaction      The executed transaction. A null pointer results in
 *      a no-op (handled by `canHaveNFTokenOfferID`).
 *  @param transactionMeta  Read-only transaction metadata used to locate the
 *      created `NFTokenOffer` node.
 */
void
insertNFTokenOfferID(
    json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    if (!canHaveNFTokenOfferID(transaction, transactionMeta))
        return;

    std::optional<uint256> result = getOfferIDFromCreatedOffer(transactionMeta);

    if (result.has_value())
        response[jss::offer_id] = to_string(result.value());
}

}  // namespace xrpl
