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
//  passphrase: <string>
// }
Json::Value doWalletPropose (RPC::Context& context)
{
    context.lock_.unlock ();

    RippleAddress   naSeed;
    RippleAddress   naAccount;

    if (!context.params_.isMember ("passphrase"))
        naSeed.setSeedRandom ();

    else if (!naSeed.setSeedGeneric (context.params_["passphrase"].asString ()))
        return rpcError(rpcBAD_SEED);

    RippleAddress naGenerator = RippleAddress::createGeneratorPublic (naSeed);
    naAccount.setAccountPublic (naGenerator, 0);

    Json::Value obj (Json::objectValue);

    obj["master_seed"]      = naSeed.humanSeed ();
    obj["master_seed_hex"]  = to_string (naSeed.getSeed ());
    obj["master_key"]     = naSeed.humanSeed1751();
    obj["account_id"]       = naAccount.humanAccountID ();
    obj["public_key"] = naAccount.humanAccountPublic();

    auto acct = naAccount.getAccountPublic();
    obj["public_key_hex"] = strHex(acct.begin(), acct.size());

    return obj;
}

} // ripple
