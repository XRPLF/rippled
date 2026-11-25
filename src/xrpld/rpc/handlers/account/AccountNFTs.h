#pragma once

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/SpecBridge.hpp>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STObject.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/account_nfts/Types.hpp>

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace xrpl::rpc {

/**
 * @brief Handles the `account_nfts` RPC method.
 *
 * The request is parsed by `::rpc::spec::handlers::account_nfts`.
 */
class AccountNFTsHandler : public HandlerFor<::rpc::spec::handlers::account_nfts::Input>
{
public:
    /**
     * @brief The data an `account_nfts` request produces, written out by writeResult().
     */
    struct Output
    {
        std::shared_ptr<ReadView const> ledger;  ///< The ledger the request selected.
        AccountID account;                       ///< The account; written only on the last page.
        std::uint32_t limit = 0;                 ///< The limit used; written only with a marker.
        std::vector<STObject> nfts;              ///< The NFTs on this page.
        std::optional<uint256> marker;           ///< Where the next page starts, if there is one.
    };

    /**
     * @brief Construct a handler for a single request.
     *
     * @param context The context of the request being handled.
     */
    explicit AccountNFTsHandler(JsonContext& context);

    /**
     * @brief Process the request.
     *
     * @param input The request, parsed and validated by the spec.
     * @return The output to write, or the status of the error.
     */
    [[nodiscard]] std::expected<Output, ::rpc::Status>
    process(Input const& input) const;

    /**
     * @brief Write the output of process() into the response.
     *
     * @param value The response to write into.
     * @param output The output process() returned.
     */
    void
    writeResult(json::Value& value, Output const& output) const;

private:
    std::reference_wrapper<JsonContext> context_;
};

}  // namespace xrpl::rpc
