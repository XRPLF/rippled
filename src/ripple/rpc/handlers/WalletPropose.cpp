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
#include <ripple/basics/strHex.h>
#include <ripple/crypto/KeyType.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Seed.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/KeypairForSignature.h>
#include <ripple/rpc/handlers/WalletPropose.h>
#include <ed25519-donna/ed25519.h>
#include <boost/optional.hpp>

namespace ripple {

// {
//  passphrase: <string>
// }
Json::Value doWalletPropose (RPC::Context& context)
{
    return walletPropose (context.params);
}

Json::Value walletPropose (Json::Value const& params)
{
    boost::optional<Seed> seed;

    KeyType keyType = KeyType::secp256k1;

    if (params.isMember (jss::key_type))
    {
        keyType = keyTypeFromString (
            params[jss::key_type].asString());

        if (keyType == KeyType::invalid)
            return rpcError(rpcINVALID_PARAMS);

        if (params.isMember (jss::passphrase) || params.isMember (jss::seed) ||
            params.isMember (jss::seed_hex))
        {
            seed = RPC::getSeedFromRPC (params);
        }
        else
        {
            seed = randomSeed ();
        }
    }
    else if (params.isMember (jss::passphrase))
    {
        seed = parseGenericSeed (
            params[jss::passphrase].asString());
    }
    else
    {
        seed = randomSeed ();
    }

    if (!seed)
        return rpcError(rpcBAD_SEED);

    auto const publicKey = generateKeyPair (keyType, *seed).first;

    Json::Value obj (Json::objectValue);

    obj[jss::master_seed] = toBase58 (*seed);
    obj[jss::master_seed_hex] = strHex (seed->data(), seed->size());
    obj[jss::master_key] = seedAs1751 (*seed);
    obj[jss::account_id] = toBase58(calcAccountID(publicKey));
    obj[jss::public_key] = toBase58(TOKEN_ACCOUNT_PUBLIC, publicKey);
    obj[jss::key_type] = to_string (keyType);
    obj[jss::public_key_hex] = strHex (publicKey.data(), publicKey.size());

    return obj;
}

} // ripple
