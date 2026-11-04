#pragma once

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/SpecBridge.hpp>

#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/transaction_entry/Types.hpp>

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace xrpl::rpc {

/**
 * @brief Handles the `transaction_entry` RPC method.
 *
 * The request is parsed by `::rpc::spec::handlers::transaction_entry`.
 */
class TransactionEntryHandler : public HandlerFor<::rpc::spec::handlers::transaction_entry::Input>
{
public:
    /**
     * @brief The data a `transaction_entry` request produces, written out by writeResult().
     */
    struct Output
    {
        std::shared_ptr<ReadView const> ledger;  ///< The ledger the request selected.
        std::optional<std::string_view> error;   ///< An error written with the ledger fields.
        std::shared_ptr<STTx const> tx;          ///< The transaction found.
        std::shared_ptr<STObject const> meta;    ///< The metadata of the transaction found.
    };

    /**
     * @brief Construct a handler for a single request.
     *
     * @param context The context of the request being handled.
     */
    explicit TransactionEntryHandler(JsonContext& context);

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
