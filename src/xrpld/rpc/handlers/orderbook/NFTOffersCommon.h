#pragma once

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/SpecBridge.hpp>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/nft_offers_common/Types.hpp>

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <vector>

namespace xrpl::rpc {

/**
 * @brief The shared body of the `nft_buy_offers` and `nft_sell_offers` handlers.
 *
 * The request is parsed by `::rpc::spec::handlers::nft_offers_common`. The two handlers
 * differ only in the offer directory they enumerate.
 */
class NFTOffersHandlerBase : public HandlerFor<::rpc::spec::handlers::nft_offers_common::Input>
{
public:
    /**
     * @brief The data an NFT offers request produces, written out by writeResult().
     */
    struct Output
    {
        UInt256 nftID;                           ///< The NFT whose offers are listed.
        std::uint32_t limit = 0;                 ///< The limit used; written only with a marker.
        std::vector<SLE::const_pointer> offers;  ///< The offers on this page.
        std::optional<UInt256> marker;           ///< Where the next page starts, if there is one.
    };

    /**
     * @brief Write the output of process() into the response.
     *
     * @param value The response to write into.
     * @param output The output process() returned.
     */
    static void
    writeResult(json::Value& value, Output const& output);

protected:
    /**
     * @brief Construct a handler for a single request.
     *
     * @param context The context of the request being handled.
     */
    explicit NFTOffersHandlerBase(JsonContext& context);

    /**
     * @brief Enumerate one page of an NFT offer directory.
     *
     * @param input The request, parsed and validated by the spec.
     * @param directory The buy or sell offer directory of the NFT.
     * @return The output to write, or the status of the error.
     */
    [[nodiscard]] std::expected<Output, ::rpc::Status>
    enumerateOffers(Input const& input, Keylet const& directory) const;

private:
    std::reference_wrapper<JsonContext> context_;
};

}  // namespace xrpl::rpc
