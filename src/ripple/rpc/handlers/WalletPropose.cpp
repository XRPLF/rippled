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
#include <ripple/crypto/SignatureAlgorithm.h>
#include <ripple/protocol/Seed.h>
#include <ripple/rpc/handlers/WalletPropose.h>
#include <ed25519-donna/ed25519.h>

namespace ripple {

// {
//  passphrase: <string>
// }
Json::Value doWalletPropose (RPC::Context& context)
{
    return WalletPropose (context.params);
}

Json::Value WalletPropose (Json::Value const& params)
{
    RippleAddress   naSeed;
    RippleAddress   naAccount;

    SignatureAlgorithm a = secp256k1;

    bool const has_algorithm  = params.isMember ("algorithm");
    bool const has_passphrase = params.isMember ("passphrase");

    if (has_algorithm)
    {
        // `algorithm` must be valid if present.

        a = AlgorithmFromString (params["algorithm"].asString());

        if (isInvalid (a))
        {
            return rpcError (rpcBAD_SEED);
        }

        naSeed = GetSeedFromRPC (params);
    }
    else if (has_passphrase)
    {
        naSeed.setSeedGeneric (params["passphrase"].asString());
    }
    else
    {
        naSeed.setSeedRandom();
    }

    if (!naSeed.isSet())
    {
        return rpcError(rpcBAD_SEED);
    }

    if (a == secp256k1)
    {
        RippleAddress naGenerator = RippleAddress::createGeneratorPublic (naSeed);
        naAccount.setAccountPublic (naGenerator, 0);
    }
    else
    {
        uint256 secretkey = KeyFromSeed (naSeed.getSeed());

        Blob publickey (33);
        publickey[0] = 0xED;
        ed25519_publickey (secretkey.data(), &publickey[1]);
        secretkey.zero();  // security erase

        naAccount.setAccountPublic (publickey);
    }

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
