#pragma once

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/SpecBridge.hpp>

#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/get_aggregate_price/Types.hpp>

#include <expected>
#include <functional>
#include <memory>
#include <optional>

namespace xrpl::rpc {

/**
 * @brief Handles the `get_aggregate_price` RPC method.
 *
 * The request is parsed by `::rpc::spec::handlers::get_aggregate_price`, which on xrpld defers
 * the error of a bad `oracles` element, so that it is reported here, after the ledger is
 * resolved, with the ledger fields.
 */
class GetAggregatePriceHandler
    : public HandlerFor<::rpc::spec::handlers::get_aggregate_price::Input>
{
public:
    /**
     * @brief The data a `get_aggregate_price` request produces, written out by writeResult().
     */
    struct Output
    {
        std::shared_ptr<ReadView const> ledger;  ///< The ledger the request selected.
        std::optional<::rpc::Status> error;      ///< An error written with the ledger fields.
        json::Value stats;                       ///< The aggregated prices.
    };

    /**
     * @brief Construct a handler for a single request.
     *
     * @param context The context of the request being handled.
     */
    explicit GetAggregatePriceHandler(JsonContext& context);

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
