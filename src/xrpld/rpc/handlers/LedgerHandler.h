#ifndef XRPL_RPC_HANDLERS_LEDGER_H_INCLUDED
#define XRPL_RPC_HANDLERS_LEDGER_H_INCLUDED

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/json/Object.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/jss.h>

namespace Json {
class Object;
}

namespace ripple {
namespace RPC {

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

    template <class Object>
    void
    writeResult(Object&);

    static constexpr char name[] = "ledger";

    static constexpr unsigned minApiVer = RPC::apiMinimumSupportedVersion;

    static constexpr unsigned maxApiVer = RPC::apiMaximumValidVersion;

    static constexpr Role role = Role::USER;

    static constexpr Condition condition = NO_CONDITION;

private:
    JsonContext& context_;
    std::shared_ptr<ReadView const> ledger_;
    std::vector<TxQ::TxDetails> queueTxs_;
    Json::Value result_;
    int options_ = 0;
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
// Implementation.

template <class Object>
void
LedgerHandler::writeResult(Object& value)
{
    if (ledger_)
    {
        Json::copyFrom(value, result_);
        addJson(value, {*ledger_, &context_, options_, queueTxs_});
    }
    else
    {
        auto& master = context_.app.getLedgerMaster();
        {
            auto&& closed = Json::addObject(value, jss::closed);
            addJson(closed, {*master.getClosedLedger(), &context_, 0});
        }
        {
            auto&& open = Json::addObject(value, jss::open);
            addJson(open, {*master.getCurrentLedger(), &context_, 0});
        }
    }

    Json::Value warnings{Json::arrayValue};
    if (context_.params.isMember(jss::type))
    {
        Json::Value& w = warnings.append(Json::objectValue);
        w[jss::id] = warnRPC_FIELDS_DEPRECATED;
        w[jss::message] =
            "Some fields from your request are deprecated. Please check the "
            "documentation at "
            "https://xrpl.org/docs/references/http-websocket-apis/ "
            "and update your request. Field `type` is deprecated.";
    }

    if (warnings.size())
        value[jss::warnings] = std::move(warnings);
}

}  // namespace RPC
}  // namespace ripple

#endif
