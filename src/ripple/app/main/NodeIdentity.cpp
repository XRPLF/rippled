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

#include <ripple/app/main/Application.h>
#include <ripple/app/main/NodeIdentity.h>
#include <ripple/app/rdb/Wallet.h>
#include <ripple/core/Config.h>
#include <ripple/core/ConfigSections.h>
#include <boost/optional.hpp>

namespace ripple {

std::pair<PublicKey, SecretKey>
getNodeIdentity(
    Application& app,
    boost::program_options::variables_map const& cmdline)
{
    std::optional<Seed> seed;

    if (cmdline.count("nodeid"))
    {
        seed = parseGenericSeed(cmdline["nodeid"].as<std::string>(), false);

        if (!seed)
            Throw<std::runtime_error>("Invalid 'nodeid' in command line");
    }
    else if (app.config().exists(SECTION_NODE_SEED))
    {
        seed = parseBase58<Seed>(
            app.config().section(SECTION_NODE_SEED).lines().front());

        if (!seed)
            Throw<std::runtime_error>("Invalid [" SECTION_NODE_SEED
                                      "] in configuration file");
    }

    if (seed)
    {
        auto secretKey = generateSecretKey(KeyType::secp256k1, *seed);
        auto publicKey = derivePublicKey(KeyType::secp256k1, secretKey);

        return {publicKey, secretKey};
    }

    auto db = app.getWalletDB().checkoutDb();

    if (cmdline.count("newnodeid") != 0)
        clearNodeIdentity(*db);

    return getNodeIdentity(*db);
}

}  // namespace ripple
