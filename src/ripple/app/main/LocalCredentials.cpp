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
#include <ripple/core/DatabaseCon.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/main/LocalCredentials.h>
#include <ripple/app/misc/UniqueNodeList.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/core/Config.h>
#include <boost/optional.hpp>
#include <iostream>

namespace ripple {

LocalCredentials::LocalCredentials(Application& app)
    : app_ (app)
{
}

void LocalCredentials::start ()
{
    // We need our node identity before we begin networking.
    // - Allows others to identify if they have connected multiple times.
    // - Determines our CAS routing and responsibilities.
    // - This is not our validation identity.
    if (!nodeIdentityLoad ())
    {
        nodeIdentityCreate ();

        if (!nodeIdentityLoad ())
            Throw<std::runtime_error> ("unable to retrieve new node identity.");
    }

    if (!app_.config().QUIET)
        std::cerr << "NodeIdentity: " << mNodePublicKey.humanNodePublic () << std::endl;

    app_.getUNL ().start ();
}

// Retrieve network identity.
bool LocalCredentials::nodeIdentityLoad ()
{
    auto db = app_.getWalletDB ().checkoutDb ();
    bool        bSuccess    = false;

    boost::optional<std::string> pubKO, priKO;
    soci::statement st = (db->prepare <<
                          "SELECT PublicKey, PrivateKey "
                          "FROM NodeIdentity;",
                          soci::into(pubKO),
                          soci::into(priKO));
    st.execute ();
    while (st.fetch ())
    {
        mNodePublicKey.setNodePublic (pubKO.value_or(""));
        mNodePrivateKey.setNodePrivate (priKO.value_or(""));

        bSuccess    = true;
    }

    if (app_.config().NODE_PUB.isValid () && app_.config().NODE_PRIV.isValid ())
    {
        mNodePublicKey = app_.config().NODE_PUB;
        mNodePrivateKey = app_.config().NODE_PRIV;
    }

    return bSuccess;
}

// Create and store a network identity.
bool LocalCredentials::nodeIdentityCreate ()
{
    if (!app_.config().QUIET)
        std::cerr << "NodeIdentity: Creating." << std::endl;

    //
    // Generate the public and private key
    //
    RippleAddress   naSeed          = RippleAddress::createSeedRandom ();
    RippleAddress   naNodePublic    = RippleAddress::createNodePublic (naSeed);
    RippleAddress   naNodePrivate   = RippleAddress::createNodePrivate (naSeed);

    //
    // Store the node information
    //
    auto db = app_.getWalletDB ().checkoutDb ();

    *db << str (boost::format (
        "INSERT INTO NodeIdentity (PublicKey,PrivateKey) VALUES ('%s','%s');")
            % naNodePublic.humanNodePublic ()
            % naNodePrivate.humanNodePrivate ());

    if (!app_.config().QUIET)
        std::cerr << "NodeIdentity: Created." << std::endl;

    return true;
}

} // ripple
