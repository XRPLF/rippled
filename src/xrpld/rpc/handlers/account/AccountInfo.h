#pragma once

#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/SpecBridge.hpp>

#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/account_info/Types.hpp>

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace xrpl::rpc {

/**
 * @brief Handles the `account_info` RPC method.
 *
 * The request is parsed by `::rpc::spec::handlers::account_info`.
 */
class AccountInfoHandler : public HandlerFor<::rpc::spec::handlers::account_info::Input>
{
public:
    /**
     * @brief The data an `account_info` request produces, written out by writeResult().
     */
    struct Output
    {
        std::shared_ptr<ReadView const> ledger;        ///< The ledger the request selected.
        std::optional<ErrorCodeI> error;               ///< An error written with the ledger fields.
        AccountID account;                             ///< The account; written only if not found.
        SLE::const_pointer accountRoot;                ///< The account root, once it was read.
        std::optional<SLE::const_pointer> signerList;  ///< The requested signer list, or null.
        std::optional<std::vector<TxQ::TxDetails>> queue;  ///< The requested queued transactions.
    };

    /**
     * @brief Construct a handler for a single request.
     *
     * @param context The context of the request being handled.
     */
    explicit AccountInfoHandler(JsonContext& context);

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
