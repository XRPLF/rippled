#pragma once

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/RPCHandler.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/server/NetworkOPs.h>

namespace json {
class Object;
}  // namespace json

namespace xrpl::RPC {

// Under what condition can we call this RPC?
enum class Condition {
    NoCondition = 0,
    NeedsNetworkConnection = 1,
    NeedsCurrentLedger = 1 << 1,
    NeedsClosedLedger = 1 << 2,
};

struct Handler
{
    template <class JsonValue>
    using Method = std::function<Status(JsonContext&, JsonValue&)>;

    char const* name;
    Method<json::Value> valueMethod;
    Role role;
    RPC::Condition condition;

    unsigned minApiVer = kApiMinimumSupportedVersion;
    unsigned maxApiVer = kApiMaximumValidVersion;
};

Handler const*
getHandler(unsigned int version, bool betaEnabled, std::string const&);

/** Return a json::ValueType::Object with a single entry. */
template <class Value>
json::Value
makeObjectValue(Value const& value, json::StaticString const& field = jss::message)
{
    json::Value result(json::ValueType::Object);
    result[field] = value;
    return result;
}

/** Return names of all methods. */
std::set<char const*>
getHandlerNames();

template <class T>
ErrorCodeI
conditionMet(Condition conditionRequired, T& context)
{
    if (context.app.getOPs().isAmendmentBlocked() && (conditionRequired != Condition::NoCondition))
    {
        return RpcAmendmentBlocked;
    }

    if (context.app.getOPs().isUNLBlocked() && (conditionRequired != Condition::NoCondition))
    {
        return RpcExpiredValidatorList;
    }

    if ((conditionRequired != Condition::NoCondition) &&
        (context.netOps.getOperatingMode() < OperatingMode::SYNCING))
    {
        JLOG(context.j.info()) << "Insufficient network mode for RPC: "
                               << context.netOps.strOperatingMode();

        if (context.apiVersion == 1)
            return RpcNoNetwork;
        return RpcNotSynced;
    }

    if (!context.app.config().standalone() && conditionRequired != Condition::NoCondition)
    {
        if (context.ledgerMaster.getValidatedLedgerAge() > Tuning::kMaxValidatedLedgerAge)
        {
            if (context.apiVersion == 1)
                return RpcNoCurrent;
            return RpcNotSynced;
        }

        auto const cID = context.ledgerMaster.getCurrentLedgerIndex();
        auto const vID = context.ledgerMaster.getValidLedgerIndex();

        if (cID + 10 < vID)
        {
            JLOG(context.j.debug()) << "Current ledger ID(" << cID
                                    << ") is less than validated ledger ID(" << vID << ")";
            if (context.apiVersion == 1)
                return RpcNoCurrent;
            return RpcNotSynced;
        }
    }

    if ((conditionRequired != Condition::NoCondition) && !context.ledgerMaster.getClosedLedger())
    {
        if (context.apiVersion == 1)
            return RpcNoClosed;
        return RpcNotSynced;
    }

    return RpcSuccess;
}

}  // namespace xrpl::RPC
