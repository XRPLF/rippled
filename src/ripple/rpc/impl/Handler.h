//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_RPC_HANDLER_H_INCLUDED
#define RIPPLE_RPC_HANDLER_H_INCLUDED

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/core/Config.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/rpc/Status.h>
#include <ripple/rpc/impl/Tuning.h>
#include <vector>

namespace Json {
class Object;
}

namespace ripple {
namespace RPC {

// Under what condition can we call this RPC?
enum Condition {
    NO_CONDITION = 0,
    NEEDS_NETWORK_CONNECTION = 1,
    NEEDS_CURRENT_LEDGER = 2 + NEEDS_NETWORK_CONNECTION,
    NEEDS_CLOSED_LEDGER = 4 + NEEDS_NETWORK_CONNECTION,
};

struct Handler
{
    template <class JsonValue>
    using Method = std::function<Status(JsonContext&, JsonValue&)>;

    const char* name_;
    Method<Json::Value> valueMethod_;
    Role role_;
    RPC::Condition condition_;
};

Handler const*
getHandler(unsigned int version, std::string const&);

/** Return a Json::objectValue with a single entry. */
template <class Value>
Json::Value
makeObjectValue(
    Value const& value,
    Json::StaticString const& field = jss::message)
{
    Json::Value result(Json::objectValue);
    result[field] = value;
    return result;
}

/** Return names of all methods. */
std::vector<char const*>
getHandlerNames();

template <class T>
error_code_i
conditionMet(Condition condition_required, T& context)
{
    if ((condition_required & NEEDS_NETWORK_CONNECTION) &&
        (context.netOps.getOperatingMode() < OperatingMode::SYNCING))
    {
        JLOG(context.j.info()) << "Insufficient network mode for RPC: "
                               << context.netOps.strOperatingMode();

        if (context.apiVersion == 1)
            return rpcNO_NETWORK;
        return rpcNOT_SYNCED;
    }

    if (context.app.getOPs().isAmendmentBlocked() &&
        (condition_required & NEEDS_CURRENT_LEDGER ||
         condition_required & NEEDS_CLOSED_LEDGER))
    {
        return rpcAMENDMENT_BLOCKED;
    }

    if (!context.app.config().standalone() &&
        condition_required & NEEDS_CURRENT_LEDGER)
    {
        if (context.ledgerMaster.getValidatedLedgerAge() >
            Tuning::maxValidatedLedgerAge)
        {
            if (context.apiVersion == 1)
                return rpcNO_CURRENT;
            return rpcNOT_SYNCED;
        }

        auto const cID = context.ledgerMaster.getCurrentLedgerIndex();
        auto const vID = context.ledgerMaster.getValidLedgerIndex();

        if (cID + 10 < vID)
        {
            JLOG(context.j.debug())
                << "Current ledger ID(" << cID
                << ") is less than validated ledger ID(" << vID << ")";
            if (context.apiVersion == 1)
                return rpcNO_CURRENT;
            return rpcNOT_SYNCED;
        }
    }

    if ((condition_required & NEEDS_CLOSED_LEDGER) &&
        !context.ledgerMaster.getClosedLedger())
    {
        if (context.apiVersion == 1)
            return rpcNO_CLOSED;
        return rpcNOT_SYNCED;
    }

    return rpcSUCCESS;
}

}  // namespace RPC
}  // namespace ripple

#endif
