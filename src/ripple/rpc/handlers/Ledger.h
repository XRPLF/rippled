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

#include <ripple/rpc/impl/HandlerBase.h>

namespace ripple {
namespace RPC {

class Object;

class LedgerHandler : public HandlerBase {
public:
    explicit LedgerHandler (Context&);
    bool check (Json::Value& error) override;
    void write (Object& value) override;

    template <class JsonValue> void writeJson (JsonValue&);

private:
    bool needsLedger_;
    Ledger::pointer ledger_;
    Json::Value lookupResult_;
    int options_;
};

} // RPC
} // ripple

#endif
