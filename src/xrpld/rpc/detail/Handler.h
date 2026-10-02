#pragma once

#include <xrpld/rpc/RPCHandler.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/StringUtilities.h>
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
    /**
     * The function a handler dispatches to.
     *
     * A plain function pointer, not a std::function: every method is a free
     * function known at compile time, so nothing needs to be captured. That
     * keeps Handler a literal type, letting the dispatch table be built and
     * checked at compile time.
     *
     * of() takes the function as a template argument, and there is no default
     * constructor, so a table entry that omits its method does not compile.
     *
     * The pointer is not also checked against null, because gcc under
     * -fsanitize=undefined does not fold the address of a function template
     * instantiation in a constant expression. A null check in the table
     * assertion, or a requires clause on Fn, both fail to compile there.
     */
    class Method
    {
    public:
        using Function = Status (*)(JsonContext&, json::Value&);

        /**
         * Build a Method that calls a given function.
         *
         * @tparam Fn The function to call.
         * @return The Method.
         */
        template <Function Fn>
        static constexpr Method
        of() noexcept
        {
            return Method{Fn};
        }

        /**
         * Call the function.
         *
         * @param context The request being served.
         * @param result The object the function writes its reply into.
         * @return The status the function returns.
         */
        Status
        operator()(JsonContext& context, json::Value& result) const
        {
            return fn_(context, result);
        }

    private:
        constexpr explicit Method(Function fn) noexcept : fn_(fn)
        {
        }

        Function fn_;
    };

    std::string_view name;
    Method valueMethod;
    Role role;
    rpc::Condition condition;

    unsigned minApiVer = kApiMinimumSupportedVersion;
    unsigned maxApiVer = kApiMaximumValidVersion;

    // Whether the command-line client accepts this method as a command. The
    // exceptions are methods whose arguments have no positional form. A field
    // rather than a comment, so that RPCCall_test can check it against the
    // command-line table in both directions.
    bool hasCommandLineForm = true;
};

/**
 * Find the handler that answers a method at an API version.
 *
 * @param version The API version the request asks for.
 * @param betaEnabled Whether the beta API version is enabled, without which
 *        @p version cannot exceed kApiMaximumSupportedVersion.
 * @param name The method name, matched exactly.
 * @return The handler, or nullptr if the version is not served, no method has
 *         this name, or the method is not served at this version. The pointer is
 *         into the dispatch table, so it outlives every caller.
 */
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
 * The names refer to storage that outlives the program, so they are safe to
 * hold on to, and each reaches its terminating null, so a caller may read one
 * as a C string.
 */
std::span<NullTerminatedView const>
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
