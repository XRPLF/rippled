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

#ifndef RIPPLED_RIPPLE_RPC_HANDLERS_LEDGER_H
#define RIPPLED_RIPPLE_RPC_HANDLERS_LEDGER_H

#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/rpc/impl/JsonObject.h>
#include <ripple/server/Role.h>

namespace ripple {
namespace RPC {

class Object;

// ledger [id|index|current|closed] [full]
// {
//    ledger: 'current' | 'closed' | <uint256> | <number>,  // optional
//    full: true | false    // optional, defaults to false.
// }

class LedgerHandler {
public:
    explicit LedgerHandler (Context&);

    Status check ();

    template <class Object>
    void writeResult (Object&);

    static const char* const name()
    {
        return "ledger";
    }

    static Role role()
    {
        return Role::USER;
    }

    static Condition condition()
    {
        return NEEDS_NETWORK_CONNECTION;
    }

private:
    Context& context_;
    Ledger::pointer ledger_;
    Json::Value result_;
    int options_;
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
// Implementation.

template <class Object>
void LedgerHandler::writeResult (Object& value)
{
    if (ledger_)
    {
        RPC::copyFrom (value, result_);
        addJson (*ledger_, value, options_, context_.yield);
    }
    else
    {
        auto& master = getApp().getLedgerMaster ();
        auto& yield = context_.yield;
        {
            auto&& closed = RPC::addObject (value, jss::closed);
            addJson (*master.getClosedLedger(), closed, 0, yield);
        }
        {
            auto&& open = RPC::addObject (value, jss::open);
            addJson (*master.getCurrentLedger(), open, 0, yield);
        }

    }
}

} // RPC
} // ripple

#endif
