#ifndef XRPL_PROTOCOL_NFTOKENID_H_INCLUDED
#define XRPL_PROTOCOL_NFTOKENID_H_INCLUDED

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxMeta.h>

#include <memory>
#include <optional>
#include <vector>

namespace ripple {

/**
   Add a `nftoken_ids` field to the `meta` output parameter.
   The field is only added to successful NFTokenMint, NFTokenAcceptOffer,
   and NFTokenCancelOffer transactions.

   Helper functions are not static because they can be used by Clio.
   @{
 */
bool
canHaveNFTokenID(
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta);

std::optional<uint256>
getNFTokenIDFromPage(TxMeta const& transactionMeta);

std::vector<uint256>
getNFTokenIDFromDeletedOffer(TxMeta const& transactionMeta);

void
insertNFTokenID(
    Json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta);
/** @} */

}  // namespace ripple

#endif
