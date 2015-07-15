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

#include <BeastConfig.h>
#include <ripple/rpc/impl/Accounts.h>
#include <ripple/rpc/impl/Utilities.h>
#include <ripple/app/main/Application.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/types.h>

namespace ripple {
namespace RPC {

Json::Value accounts (
    std::shared_ptr <ReadView const> const& lrLedger,
    RippleAddress const& naMasterGenerator,
    NetworkOPs& netOps)
{
    Json::Value jsonAccounts (Json::arrayValue);

    // YYY Don't want to leak to thin server that these accounts are related.
    // YYY Would be best to alternate requests to servers and to cache results.
    unsigned int    uIndex  = 0;

    do
    {
        // VFALCO Should be PublicKey and Generator
        RippleAddress pk;
        pk.setAccountPublic (naMasterGenerator, uIndex++);

        auto const sle =
            lrLedger->read (keylet::account(calcAccountID(pk)));

        if (sle)
        {
            Json::Value jsonAccount (Json::objectValue);

            injectSLE(jsonAccount, *sle);

            jsonAccounts.append (jsonAccount);
        }
        else
        {
            uIndex  = 0;
        }
    }
    while (uIndex);

    return jsonAccounts;
}

} // RPC
} // ripple
