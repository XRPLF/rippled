#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/TxQ.h>  // IWYU pragma: keep
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/MethodNames.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/Handler.h>
#include <xrpld/rpc/detail/SpecBridge.hpp>

#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ApiVersion.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/ledger/Types.hpp>

#include <expected>
#include <functional>
#include <memory>
#include <string_view>
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
    process(Input const& input) const;

    void
    writeResult(json::Value& value, Output const& output) const;

    // NOLINTBEGIN(readability-identifier-naming)
    static constexpr std::string_view name = method::kLedger;

    static constexpr unsigned minApiVer = rpc::kApiMinimumSupportedVersion;

    static constexpr unsigned maxApiVer = rpc::kApiMaximumValidVersion;

    static constexpr Role role = Role::USER;

    static constexpr Condition condition = Condition::NoCondition;
    // NOLINTEND(readability-identifier-naming)

private:
    std::reference_wrapper<JsonContext> context_;
};

}  // namespace xrpl::rpc
