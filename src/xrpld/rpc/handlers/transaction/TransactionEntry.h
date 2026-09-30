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

class TransactionEntryHandler : public HandlerFor<::rpc::spec::handlers::transaction_entry::Input>
{
public:
    struct Output
    {
        std::shared_ptr<ReadView const> ledger;
        std::optional<std::string_view> error;
        std::shared_ptr<STTx const> tx;
        std::shared_ptr<STObject const> meta;
    };

    explicit TransactionEntryHandler(JsonContext&);

    [[nodiscard]] std::expected<Output, ::rpc::Status>
    process(Input const& input) const;

    void
    writeResult(json::Value& value, Output const& output) const;

private:
    std::reference_wrapper<JsonContext> context_;
};

}  // namespace xrpl::rpc
