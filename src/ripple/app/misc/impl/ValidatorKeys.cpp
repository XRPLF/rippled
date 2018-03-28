//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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
#include <ripple/app/misc/ValidatorKeys.h>

#include <ripple/app/misc/Manifest.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/random.h>
#include <ripple/core/Config.h>
#include <ripple/core/ConfigSections.h>
#include <beast/core/detail/base64.hpp>

namespace ripple {
ValidatorKeys::ValidatorKeys(Config const& config, beast::Journal j)
    : cookie{rand_int<std::uint64_t>(1, std::numeric_limits<std::uint64_t>::max())}
{
    if (config.exists(SECTION_VALIDATOR_TOKEN) &&
        config.exists(SECTION_VALIDATION_SEED))
    {
        configInvalid_ = true;
        JLOG(j.fatal()) << "Cannot specify both [" SECTION_VALIDATION_SEED
                           "] and [" SECTION_VALIDATOR_TOKEN "]";
        return;
    }

    if (config.exists(SECTION_VALIDATOR_TOKEN))
    {
        if (auto const token = ValidatorToken::make_ValidatorToken(
                config.section(SECTION_VALIDATOR_TOKEN).lines()))
        {
            auto const pk = derivePublicKey(
                KeyType::secp256k1, token->validationSecret);
            auto const m = Manifest::make_Manifest(
                beast::detail::base64_decode(token->manifest));
            if (! m || pk != m->signingKey)
            {
                configInvalid_ = true;
                JLOG(j.fatal())
                    << "Invalid token specified in [" SECTION_VALIDATOR_TOKEN "]";
            }
            else
            {
                secretKey = token->validationSecret;
                publicKey = pk;
                nodeID = calcNodeID(m->masterKey);
                manifest = std::move(token->manifest);
            }
        }
        else
        {
            configInvalid_ = true;
            JLOG(j.fatal())
                << "Invalid token specified in [" SECTION_VALIDATOR_TOKEN "]";
        }
    }
    else if (config.exists(SECTION_VALIDATION_SEED))
    {
        auto const seed = parseBase58<Seed>(
            config.section(SECTION_VALIDATION_SEED).lines().front());
        if (!seed)
        {
            configInvalid_ = true;
            JLOG(j.fatal()) <<
                "Invalid seed specified in [" SECTION_VALIDATION_SEED "]";
        }
        else
        {
            secretKey = generateSecretKey(KeyType::secp256k1, *seed);
            publicKey = derivePublicKey(KeyType::secp256k1, secretKey);
            nodeID = calcNodeID(publicKey);
        }
    }
}
}  // namespace ripple
