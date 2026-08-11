#pragma once

#include <xrpld/rpc/RPCHandler.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/Log.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

#include <span>
#include <string_view>

namespace xrpl::rpc {

// Under what condition can we call this RPC?
enum class Condition {
    NoCondition = 0,
    NeedsNetworkConnection = 1,
    NeedsCurrentLedger = 1 << 1,
    NeedsClosedLedger = 1 << 2,
};

struct Handler
{
    // A plain function pointer, not a std::function: every method is a free
    // function known at compile time, so nothing needs to be captured. This
    // also keeps Handler a literal type, letting the dispatch table be built
    // and checked at compile time.
    using Method = Status (*)(JsonContext&, json::Value&);

    std::string_view name;
    Method valueMethod;
    Role role;
    rpc::Condition condition;

    unsigned minApiVer = kApiMinimumSupportedVersion;
    unsigned maxApiVer = kApiMaximumValidVersion;
};

Handler const*
getHandler(unsigned int version, bool betaEnabled, std::string_view name);

/**
 * Return a json::ValueType::Object with a single entry.
 */
template <class Value>
json::Value
makeObjectValue(Value const& value, json::StaticString const& field = jss::message)
{
    json::Value result(json::ValueType::Object);
    result[field] = value;
    return result;
}

/**
 * Return the names of all methods, sorted and without duplicates.
 *
 * The names view refers to storage that outlives the program, so it is safe to
 * hold on to.
 */
std::span<std::string_view const>
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
        if (context.ledgerMaster.getValidatedLedgerAge() > tuning::kMaxValidatedLedgerAge)
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

}  // namespace xrpl::rpc
