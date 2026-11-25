#include <xrpld/rpc/handlers/orderbook/NFTBuyOffers.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/handlers/orderbook/NFTOffersCommon.h>

#include <xrpl/protocol/Indexes.h>

#include <rpcspec/Errors.hpp>

#include <expected>

namespace xrpl::rpc {

NFTBuyOffersHandler::NFTBuyOffersHandler(JsonContext& context) : NFTOffersHandlerBase(context)
{
}

std::expected<NFTBuyOffersHandler::Output, ::rpc::Status>
NFTBuyOffersHandler::process(Input const& input) const
{
    return enumerateOffers(input, keylet::nftBuys(input.nftID));
}

}  // namespace xrpl::rpc
