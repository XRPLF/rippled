//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2015 Ripple Labs Inc.

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
#include <ripple/protocol/Seed.h>
#include <ripple/protocol/Serializer.h>

namespace ripple {

// <-- seed
static uint128 SeedFromPassPhrase (std::string const& passphrase)
{
    Serializer s;

    s.addRaw (passphrase);
    // NIKB TODO this caling sequence is a bit ugly; this should be improved.
    uint256 hash256 = s.getSHA512Half ();
    uint128 result (uint128::fromVoid (hash256.data()));

    s.secureErase ();

    return result;
}

uint256 KeyFromSeed (uint128 const& seed)
{
    Serializer s;

    s.add128 (seed);
    uint256 result = s.getSHA512Half();

    s.secureErase ();

    return result;
}

RippleAddress GetSeedFromRPC (Json::Value const& params)
{
    bool const has_passphrase = params.isMember ("passphrase");
    bool const has_seed       = params.isMember ("seed");
    bool const has_seed_hex   = params.isMember ("seed_hex");

    int const n_secrets = has_passphrase + has_seed + has_seed_hex;

    if (n_secrets > 1)
    {
        // `passphrase`, `seed`, and `seed_hex` are mutually exclusive.
        return RippleAddress();
    }

    RippleAddress result;

    if (has_seed)
    {
        std::string const seed = params["seed"].asString();

        result.setSeed (seed);
    }
    else if (has_seed_hex)
    {
        uint128 seed;
        std::string const seed_hex = params["seed_hex"].asString();

        if (seed_hex.size() != 32  ||  !seed.SetHex (seed_hex, true))
        {
            return RippleAddress();
        }

        result.setSeed (seed);
    }
    else if (has_passphrase)
    {
        std::string const passphrase = params["passphrase"].asString();

        // Given `algorithm`, `passphrase` is always the passphrase.
        uint128 const seed = SeedFromPassPhrase (passphrase);
        result.setSeed (seed);
    }

    return result;
}

} // ripple
