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
#include <ripple/rpc/impl/AccountFromString.h>

namespace ripple {
namespace RPC {

boost::optional <AccountID> accountFromStringStrict (std::string const& account)
{
    boost::optional <AccountID> result;
    RippleAddress naAccount;
    if (naAccount.setAccountPublic (account))
        result = calcAccountID (naAccount);
    else
        result = parseBase58<AccountID> (account);
    return result;
}

Json::Value accountFromString (
    AccountID& result, std::string const& strIdent, bool bStrict)
{
    if (auto accountID = accountFromStringStrict (strIdent))
    {
        result = *accountID;
        return Json::objectValue;
    }

    if (bStrict)
    {
        auto id = deprecatedParseBitcoinAccountID (strIdent);
        return rpcError (id ? rpcACT_BITCOIN : rpcACT_MALFORMED);
    }

    RippleAddress   naSeed;

    // Otherwise, it must be a seed.
    if (!naSeed.setSeedGeneric (strIdent))
        return rpcError (rpcBAD_SEED);

    // We allow the use of the seeds to access #0.
    // This is poor practice and merely for debugging convenience.

    auto naGenerator = RippleAddress::createGeneratorPublic (naSeed);

    // Generator maps don't exist.  Assume it is a master
    // generator.

    RippleAddress naAccount;
    naAccount.setAccountPublic (naGenerator, 0);
    result = calcAccountID (naAccount);
    return Json::objectValue;
}

} // RPC
} // ripple
