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

namespace ripple {

LocalCredentials::LocalCredentials ()
    : mLedger (0)
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
            throw std::runtime_error ("unable to retrieve new node identity.");
    }

    if (!getConfig ().QUIET)
        Log::out() << "NodeIdentity: " << mNodePublicKey.humanNodePublic ();

    getApp().getUNL ().start ();
}

// Retrieve network identity.
bool LocalCredentials::nodeIdentityLoad ()
{
    Database*   db = getApp().getWalletDB ()->getDB ();
    DeprecatedScopedLock  sl (getApp().getWalletDB ()->getDBLock ());
    bool        bSuccess    = false;

    if (db->executeSQL ("SELECT * FROM NodeIdentity;") && db->startIterRows ())
    {
        std::string strPublicKey, strPrivateKey;

        db->getStr ("PublicKey", strPublicKey);
        db->getStr ("PrivateKey", strPrivateKey);

        mNodePublicKey.setNodePublic (strPublicKey);
        mNodePrivateKey.setNodePrivate (strPrivateKey);

        db->endIterRows ();
        bSuccess    = true;
    }

    if (getConfig ().NODE_PUB.isValid () && getConfig ().NODE_PRIV.isValid ())
    {
        mNodePublicKey = getConfig ().NODE_PUB;
        mNodePrivateKey = getConfig ().NODE_PRIV;
    }

    return bSuccess;
}

// Create and store a network identity.
bool LocalCredentials::nodeIdentityCreate ()
{
    if (!getConfig ().QUIET)
        Log::out() << "NodeIdentity: Creating.";

    //
    // Generate the public and private key
    //
    RippleAddress   naSeed          = RippleAddress::createSeedRandom ();
    RippleAddress   naNodePublic    = RippleAddress::createNodePublic (naSeed);
    RippleAddress   naNodePrivate   = RippleAddress::createNodePrivate (naSeed);

    // Make new key.
#ifdef CREATE_NEW_DH_PARAMS
    std::string strDh512 = DH_der_gen (512);

#else
    std::string strDh512 (RippleSSLContext::getRawDHParams (512));
#endif

#if 1
    std::string     strDh1024       = strDh512;             // For testing and most cases 512 is fine.
#else
    std::string     strDh1024       = DH_der_gen (1024);
#endif

    //
    // Store the node information
    //
    Database* db    = getApp().getWalletDB ()->getDB ();

    DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());
    db->executeSQL (str (boost::format ("INSERT INTO NodeIdentity (PublicKey,PrivateKey,Dh512,Dh1024) VALUES ('%s','%s',%s,%s);")
                         % naNodePublic.humanNodePublic ()
                         % naNodePrivate.humanNodePrivate ()
                         % sqlEscape (strDh512)
                         % sqlEscape (strDh1024)));
    // XXX Check error result.

    if (!getConfig ().QUIET)
        Log::out() << "NodeIdentity: Created.";

    return true;
}

bool LocalCredentials::dataDelete (const std::string& strKey)
{
    Database* db    = getApp().getRpcDB ()->getDB ();

    DeprecatedScopedLock sl (getApp().getRpcDB ()->getDBLock ());

    return db->executeSQL (str (boost::format ("DELETE FROM RPCData WHERE Key=%s;")
                                % sqlEscape (strKey)));
}

bool LocalCredentials::dataFetch (const std::string& strKey, std::string& strValue)
{
    Database* db    = getApp().getRpcDB ()->getDB ();

    DeprecatedScopedLock sl (getApp().getRpcDB ()->getDBLock ());

    bool        bSuccess    = false;

    if (db->executeSQL (str (boost::format ("SELECT Value FROM RPCData WHERE Key=%s;")
                             % sqlEscape (strKey))) && db->startIterRows ())
    {
        Blob vucData    = db->getBinary ("Value");
        strValue.assign (vucData.begin (), vucData.end ());

        db->endIterRows ();

        bSuccess    = true;
    }

    return bSuccess;
}

bool LocalCredentials::dataStore (const std::string& strKey, const std::string& strValue)
{
    Database* db    = getApp().getRpcDB ()->getDB ();

    DeprecatedScopedLock sl (getApp().getRpcDB ()->getDBLock ());

    bool        bSuccess    = false;

    return (db->executeSQL (str (boost::format ("REPLACE INTO RPCData (Key, Value) VALUES (%s,%s);")
                                 % sqlEscape (strKey)
                                 % sqlEscape (strValue)
                                )));

    return bSuccess;
}

} // ripple
