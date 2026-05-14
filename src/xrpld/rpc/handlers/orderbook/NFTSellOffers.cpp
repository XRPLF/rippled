/** @file
 *  Implements the `nft_sell_offers` RPC handler.
 *
 *  Validates the `nft_id` field and delegates all ledger traversal,
 *  pagination, and response construction to `enumerateNFTOffers`.
 *  The buy-side counterpart (`NFTBuyOffers.cpp`) is structurally
 *  identical, differing only in the keylet passed to that helper.
 */

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/handlers/orderbook/NFTOffersHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

/** Return the open sell offers for a given NFToken.
 *
 *  Performs two input-validation guards before touching the ledger:
 *  presence of `nft_id` (returns `missing_field_error` if absent) and
 *  successful 256-bit hex parse of its value (returns `invalid_field_error`
 *  if malformed). Both guards ensure `enumerateNFTOffers` always receives
 *  a structurally valid `uint256`.
 *
 *  Ledger resolution, offer-directory existence check, pagination via
 *  `marker`/`limit`, and per-offer JSON serialization are all handled
 *  by `enumerateNFTOffers` using `keylet::nftSells(nftId)` to target the
 *  sell-side offer directory for this token.
 *
 *  @param context  RPC dispatch context carrying `params`, ledger access,
 *      and load-type tracking. `context.loadType` is set to
 *      `kFEE_MEDIUM_BURDEN_RPC` by the delegate.
 *  @return JSON object containing `nft_id`, an `offers` array of
 *      `ltNFTOKEN_OFFER` entries, and an optional `marker` when the
 *      result set was truncated at `limit`.
 *  @note The default page size is 250 offers; the marker is a hex-encoded
 *      `uint256` referencing the last offer returned, allowing callers to
 *      resume exactly where the previous page ended.
 *  @see enumerateNFTOffers, doNFTBuyOffers
 */
json::Value
doNFTSellOffers(RPC::JsonContext& context)
{
    if (!context.params.isMember(jss::nft_id))
        return RPC::missingFieldError(jss::nft_id);

    uint256 nftId;

    if (!nftId.parseHex(context.params[jss::nft_id].asString()))
        return RPC::invalidFieldError(jss::nft_id);

    return enumerateNFTOffers(context, nftId, keylet::nftSells(nftId));
}

}  // namespace xrpl
