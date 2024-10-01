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

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/rpc/Context.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

Json::Value
doLedgerClosed(RPC::JsonContext& context)
{
    auto ledger = context.ledgerMaster.getClosedLedger();
    ASSERT(
        ledger != nullptr, "ripple::doLedgerClosed : non-null closed ledger");

    Json::Value jvResult;
    jvResult[jss::ledger_index] = ledger->info().seq;
    jvResult[jss::ledger_hash] = to_string(ledger->info().hash);

    return jvResult;
}

}  // namespace ripple
