#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/TxQ.h>  // IWYU pragma: keep
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/SpecBridge.hpp>

#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/ledger/Types.hpp>

#include <expected>
#include <functional>
#include <memory>
#include <vector>

namespace xrpl::rpc {

struct JsonContext;

// ledger [id|index|current|closed] [full]
// {
//    ledger: 'current' | 'closed' | <UInt256> | <number>,  // optional
//    full: true | false    // optional, defaults to false.
// }

/**
 * @brief Handles the `ledger` RPC method.
 *
 * The request is parsed by `::rpc::spec::handlers::ledger`.
 */
class LedgerHandler : public HandlerFor<::rpc::spec::handlers::ledger::Input>
{
public:
    /**
     * @brief The data a `ledger` request produces, written out by writeResult().
     */
    struct Output
    {
        std::shared_ptr<ReadView const> ledger;  ///< The ledger the request selected.
        std::vector<TxQ::TxDetails> queueTxs;    ///< The queued transactions, when requested.
        int options = 0;  ///< The LedgerFill options to write the ledger with.
    };

    /**
     * @brief Construct a handler for a single request.
     *
     * @param context The context of the request being handled.
     */
    explicit LedgerHandler(JsonContext& context);

    /**
     * @brief Process the request.
     *
     * @param input The request, parsed and validated by the spec.
     * @return The output to write, or the status of the error.
     */
    [[nodiscard]] std::expected<Output, ::rpc::Status>
    process(Input const& input);

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
