#pragma once

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/SpecBridge.hpp>

#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/UintTypes.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/account_currencies/Types.hpp>

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <set>

namespace xrpl::rpc {

/**
 * @brief Handles the `account_currencies` RPC method.
 */
class AccountCurrenciesHandler : public HandlerFor<::rpc::spec::handlers::account_currencies::Input>
{
public:
    /**
     * @brief The data a `account_currencies` request produces, written out by writeResult().
     */
    struct Output
    {
        std::shared_ptr<ReadView const> ledger;
        std::optional<ErrorCodeI> error;
        std::set<Currency> send;
        std::set<Currency> receive;
    };

    /**
     * @brief Construct a handler for a single request.
     *
     * @param context The context of the request being handled.
     */
    explicit AccountCurrenciesHandler(JsonContext& context);

    /**
     * @brief Process the request.
     *
     * @param input The request, parsed and validated by the spec.
     * @return The output to write, or the status of the error.
     */
    [[nodiscard]] std::expected<Output, ::rpc::Status>
    process(Input const& input) const;

    /**
     * @brief Write the output of a successful process() into the response.
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
