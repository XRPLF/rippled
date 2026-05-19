#pragma once

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/jss.h>

namespace json {
class Object;
}  // namespace json

namespace xrpl::RPC {

struct JsonContext;

// ledger [id|index|current|closed] [full]
// {
//    ledger: 'current' | 'closed' | <uint256> | <number>,  // optional
//    full: true | false    // optional, defaults to false.
// }

class LedgerHandler
{
public:
    explicit LedgerHandler(JsonContext&);

    Status
    check();

    void
    writeResult(json::Value&);

    // NOLINTBEGIN(readability-identifier-naming)
    static constexpr char name[] = "ledger";

    static constexpr unsigned minApiVer = RPC::kApiMinimumSupportedVersion;

    static constexpr unsigned maxApiVer = RPC::kApiMaximumValidVersion;

    static constexpr Role role = Role::USER;

    static constexpr Condition condition = Condition::NoCondition;
    // NOLINTEND(readability-identifier-naming)

private:
    JsonContext& context_;
    std::shared_ptr<ReadView const> ledger_;
    std::vector<TxQ::TxDetails> queueTxs_;
    json::Value result_;
    int options_ = 0;
};

}  // namespace xrpl::RPC
