//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Dev Null Productions, LLC

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

#ifndef RIPPLE_RPC_HANDLERS_LEDGERS_H_INCLUDED
#define RIPPLE_RPC_HANDLERS_LEDGERS_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/json/Object.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/Status.h>
#include <ripple/rpc/impl/Handler.h>
#include <ripple/rpc/Role.h>

namespace Json {
class Object;
}

namespace ripple {
namespace RPC {

struct JsonContext;

// ledgers [ids|indexes|index_range] [full]
// {
//    ledger: 'current' | 'closed' | <uint256> | <number>,  // optional
//    full: true | false    // optional, defaults to false.
// }

class LedgersHandler {
public:
    explicit LedgersHandler (JsonContext&);

    Status check ();

    template <class Object>
    void writeResult (Object&);

    static char const* name()
    {
        return "ledgers";
    }

    static Role role()
    {
        return Role::USER;
    }

    static Condition condition()
    {
        return NO_CONDITION;
    }

private:
    JsonContext& context_;
    std::vector<std::shared_ptr<ReadView const>> ledgers_;
    int options_ = 0;
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
// Implementation.

// TODO support multiple-ledgers, range of ledgers

template <class Object>
void LedgersHandler::writeResult (Object& value)
{
    Json::Value array(Json::arrayValue);

    for(uint l = 0; l < ledgers_.size(); ++l){
        Json::Value lvalue;
        addJson(lvalue, {*ledgers_[l], options_});
        array.append(lvalue);
    }

    value[jss::ledgers] = array;
}

} // RPC
} // ripple

#endif
