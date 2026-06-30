#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/handlers/orderbook/NFTOffersHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

json::Value
doNFTBuyOffers(RPC::JsonContext& context)
{
    if (!context.params.isMember(jss::nft_id))
        return RPC::missingFieldError(jss::nft_id);

    auto const& nftIdField = context.params[jss::nft_id];
    if (!nftIdField.isString())
        return RPC::expectedFieldError(jss::nft_id, "string");

    uint256 nftId;

    if (!nftId.parseHex(nftIdField.asString()))
        return RPC::invalidFieldError(jss::nft_id);

    return enumerateNFTOffers(context, nftId, keylet::nftBuys(nftId));
}

}  // namespace xrpl
