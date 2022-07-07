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

#include <ripple/basics/strHex.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/KeyType.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Seed.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/handlers/WalletPropose.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <cmath>
#include <ed25519.h>
#include <map>

namespace ripple {

double
estimate_entropy(std::string const& input)
{
    // First, we calculate the Shannon entropy. This gives
    // the average number of bits per symbol that we would
    // need to encode the input.
    std::map<int, double> freq;

    for (auto const& c : input)
        freq[c]++;

    double se = 0.0;

    for (auto const& [_, f] : freq)
    {
        (void)_;
        auto x = f / input.length();
        se += (x)*log2(x);
    }

    // We multiply it by the length, to get an estimate of
    // the number of bits in the input. We floor because it
    // is better to be conservative.
    return std::floor(-se * input.length());
}

// {
//  passphrase: <string>
// }
Json::Value
doWalletPropose(RPC::JsonContext& context)
{
    return walletPropose(context.params);
}

Json::Value
walletPropose(Json::Value const& params)
{
    std::optional<KeyType> keyType;
    std::optional<Seed> seed;
    bool rippleLibSeed = false;

    if (params.isMember(jss::key_type))
    {
        if (!params[jss::key_type].isString())
        {
            return RPC::expected_field_error(jss::key_type, "string");
        }

        keyType = keyTypeFromString(params[jss::key_type].asString());

        if (!keyType)
            return rpcError(rpcINVALID_PARAMS);
    }

    // ripple-lib encodes seed used to generate an Ed25519 wallet in a
    // non-standard way. While we never encode seeds that way, we try
    // to detect such keys to avoid user confusion.
    {
        if (params.isMember(jss::passphrase))
            seed = RPC::parseRippleLibSeed(params[jss::passphrase]);
        else if (params.isMember(jss::seed))
            seed = RPC::parseRippleLibSeed(params[jss::seed]);

        if (seed)
        {
            rippleLibSeed = true;

            // If the user *explicitly* requests a key type other than
            // Ed25519 we return an error.
            if (keyType.value_or(KeyType::ed25519) != KeyType::ed25519)
                return rpcError(rpcBAD_SEED);

            keyType = KeyType::ed25519;
        }
    }

    if (!seed)
    {
        if (params.isMember(jss::passphrase) || params.isMember(jss::seed) ||
            params.isMember(jss::seed_hex))
        {
            Json::Value err;

            seed = RPC::getSeedFromRPC(params, err);

            if (!seed)
                return err;
        }
        else
        {
            seed = randomSeed();
        }
    }

    if (!keyType)
        keyType = KeyType::secp256k1;

    auto const publicKey = generateKeyPair(*keyType, *seed).first;

    Json::Value obj(Json::objectValue);

    auto const seed1751 = seedAs1751(*seed);
    auto const seedHex = strHex(*seed);
    auto const seedBase58 = toBase58(*seed);

    obj[jss::master_seed] = seedBase58;
    obj[jss::master_seed_hex] = seedHex;
    obj[jss::master_key] = seed1751;
    obj[jss::account_id] = toBase58(calcAccountID(publicKey));
    obj[jss::public_key] = toBase58(TokenType::AccountPublic, publicKey);
    obj[jss::key_type] = to_string(*keyType);
    obj[jss::public_key_hex] = strHex(publicKey);

    // If a passphrase was specified, and it was hashed and used as a seed
    // run a quick entropy check and add an appropriate warning, because
    // "brain wallets" can be easily attacked.
    if (!rippleLibSeed && params.isMember(jss::passphrase))
    {
        auto const passphrase = params[jss::passphrase].asString();

        if (passphrase != seed1751 && passphrase != seedBase58 &&
            passphrase != seedHex)
        {
            // 80 bits of entropy isn't bad, but it's better to
            // err on the side of caution and be conservative.
            if (estimate_entropy(passphrase) < 80.0)
                obj[jss::warning] =
                    "This wallet was generated using a user-supplied "
                    "passphrase that has low entropy and is vulnerable "
                    "to brute-force attacks.";
            else
                obj[jss::warning] =
                    "This wallet was generated using a user-supplied "
                    "passphrase. It may be vulnerable to brute-force "
                    "attacks.";
        }
    }

    return obj;
}

}  // namespace ripple
