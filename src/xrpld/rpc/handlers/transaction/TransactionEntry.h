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

namespace xrpl::rpc {

class TransactionEntryHandler : public HandlerFor<::rpc::spec::handlers::transaction_entry::Input>
{
public:
    struct Output
    {
        std::shared_ptr<ReadView const> ledger;
        /**
         * The error reported beside the ledger fields when the lookup fails.
         */
        std::optional<::rpc::Status> error;
        std::shared_ptr<STTx const> tx;
        std::shared_ptr<STObject const> meta;
    };

    explicit TransactionEntryHandler(JsonContext&);

    /**
     * Looks the transaction up in the selected ledger.
     *
     * @param input The parsed request: the ledger selected, and the `tx_hash`
     *        read or the reason there is none.
     * @return The ledger with the transaction and its metadata, or the ledger
     *         with an error carrying its code: `fieldNotFoundTransaction`
     *         without a `tx_hash`, `notYetImplemented` when the ledger is the
     *         open one, `malformedRequest` when `tx_hash` is not a hex string,
     *         `transactionNotFound` when the ledger does not hold it. A ledger
     *         that cannot be selected is returned as the unexpected Status.
     */
    [[nodiscard]] std::expected<Output, ::rpc::Status>
    process(Input const& input) const;

    /**
     * Writes the reply: the ledger fields, then either the error with its
     * code and message, or the transaction and its metadata.
     *
     * @param value The reply object written into.
     * @param output The result of `process`.
     */
    void
    writeResult(json::Value& value, Output const& output) const;

private:
    std::reference_wrapper<JsonContext> context_;
};

}  // namespace xrpl::rpc
