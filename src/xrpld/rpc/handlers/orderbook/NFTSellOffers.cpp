#include <xrpld/rpc/handlers/orderbook/NFTSellOffers.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/handlers/orderbook/NFTOffersCommon.h>

#include <xrpl/protocol/Indexes.h>

#include <rpcspec/Errors.hpp>

#include <expected>

namespace xrpl::rpc {

NFTSellOffersHandler::NFTSellOffersHandler(JsonContext& context) : NFTOffersHandlerBase(context)
{
}

std::expected<NFTSellOffersHandler::Output, ::rpc::Status>
NFTSellOffersHandler::process(Input const& input) const
{
    return enumerateOffers(input, keylet::nftSells(input.nftID));
}

}  // namespace xrpl::rpc
