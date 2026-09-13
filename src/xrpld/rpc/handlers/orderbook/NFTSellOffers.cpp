#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/handlers/orderbook/NFTOffersHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

json::Value
doNFTSellOffers(rpc::JsonContext& context)
{
    if (!context.params.isMember(jss::nft_id))
        return rpc::missingFieldError(jss::nft_id);

    uint256 nftId;

    if (!nftId.parseHex(context.params[jss::nft_id].asString()))
        return rpc::invalidFieldError(jss::nft_id);

    return enumerateNFTOffers(context, nftId, keylet::nftSells(nftId));
}

}  // namespace xrpl
