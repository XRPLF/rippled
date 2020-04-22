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
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/core/DatabaseCon.h>
#include <boost/format.hpp>
#include <boost/optional.hpp>

namespace ripple {

std::pair<PublicKey, SecretKey>
loadNodeIdentity(Application& app)
{
    // If a seed is specified in the configuration file use that directly.
    if (app.config().exists(SECTION_NODE_SEED))
    {
        auto const seed = parseBase58<Seed>(
            app.config().section(SECTION_NODE_SEED).lines().front());

        if (!seed)
            Throw<std::runtime_error>("NodeIdentity: Bad [" SECTION_NODE_SEED
                                      "] specified");

        auto secretKey = generateSecretKey(KeyType::secp256k1, *seed);
        auto publicKey = derivePublicKey(KeyType::secp256k1, secretKey);

        return {publicKey, secretKey};
    }

    // Try to load a node identity from the database:
    boost::optional<PublicKey> publicKey;
    boost::optional<SecretKey> secretKey;

    auto db = app.getWalletDB().checkoutDb();

    {
        boost::optional<std::string> pubKO, priKO;
        soci::statement st =
            (db->prepare << "SELECT PublicKey, PrivateKey FROM NodeIdentity;",
             soci::into(pubKO),
             soci::into(priKO));
        st.execute();
        while (st.fetch())
        {
            auto const sk = parseBase58<SecretKey>(
                TokenType::NodePrivate, priKO.value_or(""));
            auto const pk = parseBase58<PublicKey>(
                TokenType::NodePublic, pubKO.value_or(""));

            // Only use if the public and secret keys are a pair
            if (sk && pk && (*pk == derivePublicKey(KeyType::secp256k1, *sk)))
            {
                secretKey = sk;
                publicKey = pk;
            }
        }
    }

    // If a valid identity wasn't found, we randomly generate a new one:
    if (!publicKey || !secretKey)
    {
        std::tie(publicKey, secretKey) = randomKeyPair(KeyType::secp256k1);

        *db << str(
            boost::format("INSERT INTO NodeIdentity (PublicKey,PrivateKey) "
                          "VALUES ('%s','%s');") %
            toBase58(TokenType::NodePublic, *publicKey) %
            toBase58(TokenType::NodePrivate, *secretKey));
    }

    return {*publicKey, *secretKey};
}

}  // namespace ripple
