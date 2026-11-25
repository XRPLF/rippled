#pragma once

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/SpecBridge.hpp>

#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/book_changes/Types.hpp>

#include <expected>
#include <functional>
#include <memory>

namespace xrpl::rpc {

/**
 * @brief Handles the `book_changes` RPC method.
 *
 * The request is parsed by `::rpc::spec::handlers::book_changes`.
 */
class BookChangesHandler : public HandlerFor<::rpc::spec::handlers::book_changes::Input>
{
public:
    /**
     * @brief The data a `book_changes` request produces, written out by writeResult().
     */
    struct Output
    {
        std::shared_ptr<ReadView const> ledger;  ///< The ledger the request selected.
    };

    /**
     * @brief Construct a handler for a single request.
     *
     * @param context The context of the request being handled.
     */
    explicit BookChangesHandler(JsonContext& context);

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
    static void
    writeResult(json::Value& value, Output const& output);

private:
    std::reference_wrapper<JsonContext> context_;
};

}  // namespace xrpl::rpc
