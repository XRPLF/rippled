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

class BookChangesHandler : public HandlerFor<::rpc::spec::handlers::book_changes::Input>
{
public:
    struct Output
    {
        std::shared_ptr<ReadView const> ledger;
    };

    explicit BookChangesHandler(JsonContext&);

    [[nodiscard]] std::expected<Output, ::rpc::Status>
    process(Input const& input) const;

    static void
    writeResult(json::Value& value, Output const& output);

private:
    std::reference_wrapper<JsonContext> context_;
};

}  // namespace xrpl::rpc
