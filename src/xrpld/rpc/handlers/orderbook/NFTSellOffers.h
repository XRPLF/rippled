#pragma once

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/handlers/orderbook/NFTOffersCommon.h>

#include <rpcspec/Errors.hpp>

#include <expected>

namespace xrpl::rpc {

/**
 * @brief Handles the `nft_sell_offers` RPC method.
 *
 * The request is parsed by `::rpc::spec::handlers::nft_offers_common`.
 */
class NFTSellOffersHandler : public NFTOffersHandlerBase
{
public:
    /**
     * @brief Construct a handler for a single request.
     *
     * @param context The context of the request being handled.
     */
    explicit NFTSellOffersHandler(JsonContext& context);

    /**
     * @brief Process the request.
     *
     * @param input The request, parsed and validated by the spec.
     * @return The output to write, or the status of the error.
     */
    [[nodiscard]] std::expected<Output, ::rpc::Status>
    process(Input const& input) const;
};

}  // namespace xrpl::rpc
