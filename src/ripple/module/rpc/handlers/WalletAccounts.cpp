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


namespace ripple {

// {
//   seed: <string>
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
// }
Json::Value doWalletAccounts (RPC::Context& context)
{
    Ledger::pointer     lpLedger;
    Json::Value         jvResult    = RPC::lookupLedger (context.params_, lpLedger, context.netOps_);

    if (!lpLedger)
        return jvResult;

    RippleAddress   naSeed;

    if (!context.params_.isMember ("seed") || !naSeed.setSeedGeneric (context.params_["seed"].asString ()))
    {
        return rpcError (rpcBAD_SEED);
    }

    // Try the seed as a master seed.
    RippleAddress   naMasterGenerator   = RippleAddress::createGeneratorPublic (naSeed);

    Json::Value jsonAccounts    = RPC::accounts (lpLedger, naMasterGenerator, context.netOps_);

    if (jsonAccounts.empty ())
    {
        // No account via seed as master, try seed a regular.
        Json::Value ret = RPC::getMasterGenerator (lpLedger, naSeed, naMasterGenerator, context.netOps_);

        if (!ret.empty ())
            return ret;

        ret["accounts"] = RPC::accounts (lpLedger, naMasterGenerator, context.netOps_);

        return ret;
    }
    else
    {
        // Had accounts via seed as master, return them.
        Json::Value ret (Json::objectValue);

        ret["accounts"] = jsonAccounts;

        return ret;
    }
}

} // ripple
