//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/json/json_value.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/TransactionSign.h>
#include <ripple/rpc/Role.h>

namespace ripple {

Json::Value
doHealthCheck(RPC::JsonContext& context)
{
    bool constexpr humanReadable = true;
    bool constexpr noAdmin = false;
    bool constexpr noCounters = false;
    auto info = context.netOps.getServerInfo(
                    humanReadable, noAdmin, noCounters);

    int last_validated_ledger_age = std::numeric_limits<int>::max();
    if (info.isMember("validated_ledger"))
        last_validated_ledger_age = info["validated_ledger"]["age"].asInt();
    bool amendment_blocked = false;
    if (info.isMember("amendment_blocked"))
        amendment_blocked = true;
    int number_peers = info["peers"].asInt();
    std::string server_state = info["server_state"].asString();
    auto load_factor = info["load_factor"].asDouble();

    Json::Value ret = Json::objectValue;
    enum {healthy, warning, critical};
    int health = healthy;
    auto set_health = [&health](int state)
        {
            if (health < state)
                health = state;
        };

    if (last_validated_ledger_age >= 7)
    {
        ret[jss::info]["validated_ledger"] = last_validated_ledger_age;
        if (last_validated_ledger_age < 20)
            set_health(warning);
        else
            set_health(critical);
    }

    if (amendment_blocked)
    {
        ret[jss::info]["amendment_blocked"] = true;
        set_health(critical);
    }

    if (number_peers <= 7)
    {
        ret[jss::info]["peers"] = number_peers;
        if (number_peers != 0)
            set_health(warning);
        else
            set_health(critical);
    }

    if (!(server_state == "full" || server_state == "validating" ||
          server_state == "proposing"))
    {
        ret[jss::info]["server_state"] = server_state;
        if (server_state == "syncing" || server_state == "tracking" ||
            server_state == "connected")
        {
            set_health(warning);
        }
        else
            set_health(critical);
    }

    if (load_factor > 100)
    {
        ret[jss::info]["load_factor"] = load_factor;
        if (load_factor < 1000)
            set_health(warning);
        else
            set_health(critical);
    }

    switch (health)
    {
    case healthy:
        ret["health"] = "Healthy";
        break;
    case warning:
        ret["health"] = "Warning";
        break;
    default:
        ret["health"] = "Critical";
        break;
    }
    return ret;
}

} // ripple
