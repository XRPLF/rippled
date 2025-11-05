#ifndef XRPL_PROTOCOL_NFTOKENOFFERID_H_INCLUDED
#define XRPL_PROTOCOL_NFTOKENOFFERID_H_INCLUDED

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxMeta.h>

#include <memory>
#include <optional>

namespace ripple {

/**
   Add an `offer_id` field to the `meta` output parameter.
   The field is only added to successful NFTokenCreateOffer transactions.

   Helper functions are not static because they can be used by Clio.
   @{
 */
bool
canHaveNFTokenOfferID(
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta);

std::optional<uint256>
getOfferIDFromCreatedOffer(TxMeta const& transactionMeta);

void
insertNFTokenOfferID(
    Json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta);
/** @} */

}  // namespace ripple

#endif
