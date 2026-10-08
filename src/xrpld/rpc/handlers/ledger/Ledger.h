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

class LedgerHandler : public HandlerFor<::rpc::spec::handlers::ledger::Input>
{
public:
    struct Output
    {
        std::shared_ptr<ReadView const> ledger;
        std::vector<TxQ::TxDetails> queueTxs;
        int options = 0;
    };

    explicit LedgerHandler(JsonContext&);

    [[nodiscard]] std::expected<Output, ::rpc::Status>
    process(Input const& input);

    void
    writeResult(json::Value& value, Output const& output) const;

private:
    std::reference_wrapper<JsonContext> context_;
};

}  // namespace xrpl::rpc
