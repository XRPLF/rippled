//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/rpc/KeypairForSignature.h>

namespace ripple {
namespace RPC {

boost::optional<Seed>
getSeedFromRPC (Json::Value const& params)
{
    bool const hasPassphrase = params.isMember (jss::passphrase);
    bool const hasSeed       = params.isMember (jss::seed);
    bool const hasHexSeed    = params.isMember (jss::seed_hex);

    int const count =
        (hasPassphrase ? 1 : 0) +
        (hasSeed ? 1 : 0) +
        (hasHexSeed ? 1 : 0);

    if (count == 1)
    {
        if (hasSeed)
            return parseBase58<Seed> (params[jss::seed].asString());

        if (hasPassphrase)
            return parseGenericSeed (params[jss::passphrase].asString());

        if (hasHexSeed)
        {
            uint128 seed;

            if (seed.SetHexExact (params[jss::seed_hex].asString()))
                return Seed { Slice(seed.data(), seed.size()) };

            return boost::none;
        }
    }

    return boost::none;
}

std::pair<PublicKey, SecretKey>
keypairForSignature (Json::Value const& params, Json::Value& error)
{
    bool const has_key_type  = params.isMember (jss::key_type);

    // All of the secret types we allow, but only one at a time.
    // The array should be constexpr, but that makes Visual Studio unhappy.
    static char const* const secretTypes[]
    {
        jss::passphrase.c_str(),
        jss::secret.c_str(),
        jss::seed.c_str(),
        jss::seed_hex.c_str()
    };

    // Identify which secret type is in use.
    char const* secretType = nullptr;
    int secretCount = 0;
    for (auto t : secretTypes)
    {
        if (params.isMember (t))
        {
            ++secretCount;
            secretType = t;
        }
    }

    if (secretCount == 0 || secretType == nullptr)
    {
        error = RPC::missing_field_error (jss::secret);
        return { };
    }

    if (secretCount > 1)
    {
        // `passphrase`, `secret`, `seed`, and `seed_hex` are mutually exclusive.
        error = rpcError (rpcBAD_SECRET);
        return { };
    }

    if (has_key_type && (secretType == jss::secret.c_str()))
    {
        // `secret` is deprecated.
        error = rpcError (rpcBAD_SECRET);
        return { };
    }

    KeyType keyType = KeyType::secp256k1;
    boost::optional<Seed> seed;

    if (has_key_type)
    {
        keyType = keyTypeFromString (
            params[jss::key_type].asString());

        if (keyType == KeyType::invalid)
        {
            error = rpcError (rpcBAD_SEED);
            return { };
        }

        seed = getSeedFromRPC (params);
    }
    else
    {
        seed = parseGenericSeed (
            params[jss::secret].asString ());
    }

    if (!seed)
    {
        error = RPC::make_error (rpcBAD_SEED,
            RPC::invalid_field_message (secretType));
        return { };
    }

    if (keyType != KeyType::secp256k1 && keyType != KeyType::ed25519)
        LogicError ("keypairForSignature: invalid key type");

    return generateKeyPair (keyType, *seed);
}

} // RPC
} // ripple
